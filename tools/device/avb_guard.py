"""Strict, offline AVB admission checks for this tablet's stock index values.

No OpenSSL subprocess, device I/O, signing or index adjustment. Unknown,
unsigned and malformed inputs are rejected rather than treated as index 0.
"""
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

from Crypto.Hash import SHA256
from Crypto.PublicKey import RSA
from Crypto.Signature import pkcs1_15

TESTKEY_SHA1 = '2597c218aae470a130f61162feaae70afd97f011'
INDEX = {'boot': 1738713600, 'recovery': 1, 'vbmeta': 0,
         'vbmeta_system': 1772668800}


class AdmissionError(ValueError):
    pass


def need(ok, reason):
    if not ok:
        raise AdmissionError(reason)


def base_partition(name):
    return name[:-2] if name.endswith(('_a', '_b')) else name


def digest_file(path, salt=b'', count=None):
    h = hashlib.sha256(salt)
    with open(path, 'rb') as f:
        remaining = Path(path).stat().st_size if count is None else count
        while remaining:
            block = f.read(min(1 << 20, remaining))
            need(bool(block), 'truncated image while hashing')
            h.update(block)
            remaining -= len(block)
    return h.digest()


@dataclass(frozen=True)
class CheckedImage:
    path: Path
    partition: str
    bytes: int
    sha256: str
    index: int
    location: int


def metadata(path):
    path = Path(path)
    size = path.stat().st_size
    need(size >= 256, 'image is too short for AVB')
    with path.open('rb') as f:
        f.seek(-64, 2)
        footer = f.read(64)
        if footer[:4] == b'AVBf':
            major, minor, original, offset, length = struct.unpack_from('>IIQQQ', footer, 4)
            need(major == 1 and minor == 0, 'unsupported AVB footer version')
            need(256 <= length <= 1 << 20 and original <= offset and offset + length <= size - 64,
                 'invalid AVB footer bounds')
        else:
            f.seek(0)
            need(f.read(4) == b'AVB0', 'unsigned image: AVB metadata is missing')
            original, offset, length = 0, 0, min(size, 1 << 20)
        f.seek(offset)
        blob = f.read(length)
    need(blob[:4] == b'AVB0' and len(blob) >= 256, 'invalid AVB header')
    auth_size, aux_size = struct.unpack_from('>QQ', blob, 12)
    algorithm = struct.unpack_from('>I', blob, 28)[0]
    need(algorithm == 2, 'expected SHA256_RSA4096; unsigned/other algorithm refused')
    need(256 + auth_size + aux_size <= len(blob), 'AVB blocks extend past metadata')
    need(aux_size <= 1 << 20 and auth_size <= 4096, 'excessive AVB metadata size')
    auth = blob[256:256 + auth_size]
    aux = blob[256 + auth_size:256 + auth_size + aux_size]

    def field(data, pos):
        start, count = struct.unpack_from('>QQ', blob, pos)
        need(start <= len(data) and count <= len(data) - start, 'AVB field out of bounds')
        return data[start:start + count]

    stored = field(auth, 32)
    signature = field(auth, 48)
    pk = field(aux, 64)
    desc = field(aux, 96)
    rollback = struct.unpack_from('>Q', blob, 112)[0]
    flags, location = struct.unpack_from('>II', blob, 120)
    need(flags == 0, 'AVB verification/hashtree-disabling flags refused')
    need(len(stored) == 32 and len(signature) == 512 and len(pk) == 1032,
         'unexpected RSA4096 authentication fields')
    need(hashlib.sha1(pk).hexdigest() == TESTKEY_SHA1, 'public key differs from accepted testkey')
    need(struct.unpack_from('>I', pk)[0] == 4096, 'unexpected AVB key size')
    payload = blob[:256] + aux
    need(hashlib.sha256(payload).digest() == stored, 'AVB authentication hash mismatch')
    try:
        key = RSA.construct((int.from_bytes(pk[8:520], 'big'), 65537))
        pkcs1_15.new(key).verify(SHA256.new(payload), signature)
    except (ValueError, TypeError) as e:
        raise AdmissionError('RSA signature verification failed') from e
    hashes, chains, props = {}, {}, {}
    cursor = 0
    while cursor < len(desc):
        need(cursor + 16 <= len(desc), 'truncated descriptor header')
        tag, following = struct.unpack_from('>QQ', desc, cursor)
        need(following % 8 == 0 and following <= len(desc) - cursor - 16, 'descriptor bounds/alignment invalid')
        d = desc[cursor:cursor + 16 + following]
        if tag == 2:
            need(len(d) >= 132, 'short hash descriptor')
            image_size = struct.unpack_from('>Q', d, 16)[0]
            nl, sl, dl, hash_flags = struct.unpack_from('>IIII', d, 56)
            need(132 + nl + sl + dl <= len(d), 'hash descriptor payload truncated')
            name = d[132:132 + nl].decode('ascii')
            need(name not in hashes and d[24:56].rstrip(b'\0') == b'sha256' and dl == 32,
                 'duplicate/unsupported hash descriptor')
            need(hash_flags == 0, 'hash descriptor flags refused')
            hashes[name] = (image_size, d[132 + nl:132 + nl + sl], d[132 + nl + sl:132 + nl + sl + dl])
        elif tag == 4:
            need(len(d) >= 92, 'short chain descriptor')
            loc, nl, kl = struct.unpack_from('>III', d, 16)
            need(92 + nl + kl <= len(d), 'truncated chain descriptor')
            name = d[92:92 + nl].decode('ascii')
            need(name not in chains, 'duplicate chain partition')
            chains[name] = (loc, hashlib.sha1(d[92 + nl:92 + nl + kl]).hexdigest())
        elif tag == 0:
            need(len(d) >= 32, 'short property descriptor')
            kl, vl = struct.unpack_from('>QQ', d, 16)
            need(32 + kl + 1 + vl + 1 <= len(d), 'truncated property descriptor')
            props[d[32:32 + kl].decode()] = d[33 + kl:33 + kl + vl].decode()
        cursor += 16 + following
    return {'index': rollback, 'location': location, 'hashes': hashes, 'chains': chains,
            'original_size': original, 'offset': offset, 'props': props}


def check_image(path, partition, parent_vbmeta=None):
    path = Path(path).resolve()
    name = base_partition(partition)
    size = path.stat().st_size
    need(size > 0 and size % 4096 == 0, 'image must contain complete 4096-byte sectors')
    if name in INDEX:
        meta = metadata(path)
        need(meta['index'] == INDEX[name], f'{name}: rollback index must equal {INDEX[name]} (got {meta["index"]})')
        need(meta['location'] == 0, f'{name}: rollback location must remain 0 in this image')
        if name in ('boot', 'recovery', 'vbmeta_system'):
            need(name in meta['hashes'], f'AVB image does not authenticate partition {name}')
            count, salt, digest = meta['hashes'][name]
            need(count <= size and (not meta['original_size'] or count == meta['original_size']),
                 'authenticated image length is inconsistent')
            need(digest_file(path, salt, count) == digest, 'partition data digest mismatch')
        if name == 'vbmeta':
            for child, location in {'boot': 3, 'recovery': 1, 'vbmeta_system': 2}.items():
                need(meta['chains'].get(child) == (location, TESTKEY_SHA1), f'vbmeta chain {child} location/key changed')
        index = meta['index']
    elif name in ('init_boot', 'vendor_boot', 'dtbo'):
        need(parent_vbmeta is not None, f'{name} needs an explicitly supplied signed parent vbmeta')
        check_image(parent_vbmeta, 'vbmeta')
        meta = metadata(parent_vbmeta)
        need(name in meta['hashes'], f'parent vbmeta does not authenticate {name}')
        count, salt, digest = meta['hashes'][name]
        need(count <= size and digest_file(path, salt, count) == digest, 'parent vbmeta digest mismatch')
        index = 0
    else:
        raise AdmissionError(f'no signed-image admission rule for {partition}')
    return CheckedImage(path, partition, size, digest_file(path).hex(), index, 0)
