"""Two-slot stage2 update format and offline regular-file transaction tool.

The commit record is written only after the complete FAT image is durable and
read-back verified. A retained generation is never overwritten. CRC/SHA are
integrity checks, not authentication or hardware boot certification.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import zlib

MIB = 1 << 20
HEADER = 4096
MAX_BYTES = 256 * MIB


@dataclass(frozen=True)
class Bundle:
    slot: int
    generation: int
    size: int
    span: int
    digest: bytes

    @property
    def offset(self):
        return self.slot * self.span + MIB


def encode(item):
    data = bytearray(HEADER)
    struct.pack_into('<8sIIQQ32sQII', data, 0, b'ELDSTG01', 1, HEADER,
                     item.generation, item.size, item.digest, item.span, item.slot, 0)
    struct.pack_into('<I', data, HEADER - 4, zlib.crc32(data[:-4]))
    if decode(data, item.span * 2, item.slot) != item:
        raise ValueError('invalid bundle metadata')
    return bytes(data)


def decode(data, partition_bytes, slot):
    if len(data) != HEADER or slot not in (0, 1):
        return None
    magic, version, header, generation, size, digest, saved_span, saved_slot, flags = struct.unpack_from('<8sIIQQ32sQII', data)
    span = (partition_bytes // 2) & ~(MIB - 1)
    if (magic != b'ELDSTG01' or version != 1 or header != HEADER or not generation or
        span < 64 * MIB or saved_span != span or slot != saved_slot or flags or
        size < MIB or size > min(MAX_BYTES, span - MIB) or size % 512 or
        any(data[80:-4]) or zlib.crc32(data[:-4]) != struct.unpack_from('<I', data, HEADER - 4)[0]):
        return None
    return Bundle(slot, generation, size, span, digest)


class FileStore:
    """Offline files only. Deliberately refuses raw block devices."""
    def __init__(self, path, writable=False):
        self.file = open(path, 'r+b' if writable else 'rb', buffering=0)
        info = os.fstat(self.file.fileno())
        if not stat.S_ISREG(info.st_mode):
            self.file.close()
            raise ValueError('offline tool requires a regular file')
        self.size = info.st_size

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError('read outside store')
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise IOError('short read')
        return data

    def write(self, offset, data):
        if offset < 0 or offset + len(data) > self.size:
            raise ValueError('write outside store')
        self.file.seek(offset)
        view = memoryview(data)
        while view:
            n = self.file.write(view)
            if not n:
                raise IOError('short write')
            view = view[n:]

    def sync(self):
        self.file.flush()
        os.fsync(self.file.fileno())

    def write_payload(self, offset, data):
        for at in range(0, len(data), MIB):
            self.write(offset + at, data[at:at + MIB])

    def digest(self, offset, size):
        h = hashlib.sha256()
        for at in range(0, size, MIB):
            h.update(self.read(offset + at, min(MIB, size - at)))
        return h.digest()

    def close(self):
        self.file.close()


def inventory(store):
    span = (store.size // 2) & ~(MIB - 1)
    headers = [store.read(i * span, HEADER) for i in (0, 1)]
    entries = [decode(h, store.size, i) for i, h in enumerate(headers)]
    if all(entries) and entries[0].generation == entries[1].generation:
        raise ValueError('ambiguous equal generations')
    valid = [item for item in entries if item and store.digest(item.offset, item.size) == item.digest]
    return headers, entries, valid


def checked_fat(path):
    data = Path(path).read_bytes()
    if not MIB <= len(data) <= MAX_BYTES or len(data) % 512 or data[510:512] != b'\x55\xaa':
        raise ValueError('expected complete FAT image (1..256 MiB)')
    sector = struct.unpack_from('<H', data, 11)[0]
    sectors = struct.unpack_from('<H', data, 19)[0] or struct.unpack_from('<I', data, 32)[0]
    if sector != 512 or sectors * sector != len(data):
        raise ValueError('FAT BPB does not describe the complete bundle')
    return data


def transaction(store, data, keep_generation=None, initialize=False, commit=False):
    headers, entries, valid = inventory(store)
    if initialize:
        if keep_generation is not None or any(entries) or any(any(h) for h in headers):
            raise ValueError('initialize requires empty headers and no retained generation')
        target = 0
    else:
        matches = [item for item in valid if item.generation == keep_generation]
        if len(matches) != 1:
            raise ValueError('explicit keep generation must have a verified complete payload')
        target = 1 - matches[0].slot
    span = (store.size // 2) & ~(MIB - 1)
    generation = max((i.generation for i in entries if i), default=0) + 1
    item = Bundle(target, generation, len(data), span, hashlib.sha256(data).digest())
    header = encode(item)
    report = dict(slot=target, generation=generation, bytes=len(data), sha256=item.digest.hex(),
                  offset=item.offset, header_offset=target * span, keep_generation=keep_generation,
                  committed=False, hardware_tested=False)
    if not commit:
        return report
    # Recheck the selection just before invalidating the inactive commit record.
    if [store.read(i * span, HEADER) for i in (0, 1)] != headers:
        raise ValueError('store changed after planning')
    store.write(target * span, bytes(HEADER))
    store.sync()
    store.write_payload(item.offset, data)
    store.sync()
    if store.digest(item.offset, len(data)) != item.digest:
        raise IOError('inactive payload read-back hash mismatch; not committed')
    other = 1 - target
    if store.read(other * span, HEADER) != headers[other]:
        raise ValueError('retained commit record changed; not committed')
    store.write(target * span, header)
    store.sync()
    if store.read(target * span, HEADER) != header:
        raise IOError('commit record read-back mismatch')
    report['committed'] = True
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('store', type=Path, help='existing local regular file, never a block device')
    p.add_argument('--bundle', type=Path)
    p.add_argument('--keep-generation', type=int)
    p.add_argument('--initialize', action='store_true')
    p.add_argument('--commit-local-file', action='store_true')
    a = p.parse_args()
    store = FileStore(a.store, a.commit_local_file)
    try:
        if a.bundle:
            result = transaction(store, checked_fat(a.bundle), a.keep_generation, a.initialize, a.commit_local_file)
        else:
            _, entries, valid = inventory(store)
            result = [dict(slot=i.slot, generation=i.generation, bytes=i.size, sha256=i.digest.hex(),
                           payload_verified=i in valid) for i in entries if i]
        print(json.dumps(result, indent=2))
    finally:
        store.close()


if __name__ == '__main__':
    main()
