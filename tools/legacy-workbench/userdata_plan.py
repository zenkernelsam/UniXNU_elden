"""Read-only GPT planning for a 256 GiB userdata carve-out. Emits no flash XML.

The old GPT is a planning input, not a substitute for a fresh device snapshot.
Filesystem shrink/decryption and ciphertext backup must precede GPT changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def parse(data, sector=4096):
    if sector not in (512,4096) or len(data)<2*sector:
        raise ValueError('unsupported sector size or truncated GPT')
    if data[sector:sector+8] != b'EFI PART':
        raise ValueError('expected primary GPT at LBA1')
    header = bytearray(data[sector:sector+92])
    size = struct.unpack_from('<I', header, 12)[0]
    if size != 92:
        raise ValueError('unsupported GPT header size')
    expected = struct.unpack_from('<I', header, 16)[0]
    header[16:20] = bytes(4)
    if zlib.crc32(header) & 0xffffffff != expected:
        raise ValueError('GPT header CRC mismatch')
    lba = struct.unpack_from('<Q', header, 72)[0]
    count, stride, crc = struct.unpack_from('<III', header, 80)
    current,backup,first_usable,last_usable=struct.unpack_from('<QQQQ',header,24)
    if (header[8:12]!=b'\x00\x00\x01\x00' or header[20:24]!=bytes(4) or
        current!=1 or first_usable<2 or first_usable>last_usable or last_usable>=backup or
        not 1<=count<=4096 or not 128<=stride<=4096 or stride&(stride-1) or
        lba<2 or lba>=first_usable or (count*stride+sector-1)//sector>first_usable-lba):
        raise ValueError('invalid GPT geometry')
    entries = data[lba*sector:lba*sector+count*stride]
    if len(entries) != count*stride or zlib.crc32(entries) & 0xffffffff != crc:
        raise ValueError('GPT entries CRC/bounds mismatch')
    result = {}
    identifiers=set()
    for i in range(count):
        e = entries[i*stride:(i+1)*stride]
        if e[:16] == bytes(16):
            continue
        name = e[56:128].decode('utf-16-le').rstrip('\0')
        first, last = struct.unpack_from('<QQ', e, 32)
        if not name or '\0' in name or name in result:
            raise ValueError('empty or ambiguous partition name')
        if e[16:32]==bytes(16) or e[16:32] in identifiers:
            raise ValueError('empty or duplicate unique partition GUID')
        identifiers.add(e[16:32])
        if first<first_usable or last>last_usable or first>last:
            raise ValueError('partition outside usable disk range')
        if any(first<=p['last_lba'] and p['first_lba']<=last for p in result.values()):
            raise ValueError('existing partitions overlap')
        result[name] = {'first_lba': first, 'last_lba': last, 'sectors': last-first+1,
                        'entry_sha256': hashlib.sha256(e).hexdigest()}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gpt', type=Path, default=Path('Backup/B_edl_stock/lun0/gpt_main0.bin'))
    p.add_argument('--size-gib', type=int, default=256)
    p.add_argument('--output', type=Path, default=Path('edl_work/userdata-256-plan.json'))
    a = p.parse_args()
    if a.size_gib<=0:
        raise ValueError('root capacity must be positive')
    parts = parse(a.gpt.read_bytes())
    data = parts['userdata']
    reserve = a.size_gib*(1 << 30)//4096
    # Align the new start downward to a 1 MiB boundary, ensuring at least the
    # requested root space. Derive from the original LAST LBA, not capacity labels.
    first = (data['last_lba'] + 1 - reserve) & ~255
    last = first - 1
    if first <= data['first_lba']:
        raise ValueError('requested root space consumes userdata')
    for name, item in parts.items():
        if name != 'userdata' and item['first_lba'] <= data['last_lba'] and item['last_lba'] >= first:
            raise ValueError('planned root overlaps ' + name)
    report = {'mode': 'offline plan only; no device write or encryption-key export',
              'source_gpt_sha256': hashlib.sha256(a.gpt.read_bytes()).hexdigest(),
              'root_size_request': f'{a.size_gib} GiB (binary units)',
              'userdata_before': data, 'userdata_after': {'first_lba': data['first_lba'],
                  'last_lba': last, 'sectors': last-data['first_lba']+1},
              'ipados_after': {'first_lba': first, 'last_lba': data['last_lba'],
                  'sectors': data['last_lba']-first+1,
                  'bytes': (data['last_lba']-first+1)*4096},
              'device_gate': ['fresh GPT and decrypted F2FS superblocks',
                  'verified restorable ciphertext backup', 'running kernel matches audited resize implementation',
                  'use actual decrypted superblocks and dm capacity with f2fs_shrink_plan.py; do not equate request with final size',
                  'filesystem physically shrunk below new boundary BEFORE GPT',
                  'both superblocks and checkpoint verified; metadata keys preserved'],
              'host_reencryption_from_password_supported': False}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
