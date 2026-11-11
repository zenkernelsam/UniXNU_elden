"""Read raw inode owners in the small, unencrypted APFS seed; never write it.

macOS projects anonymous owner 99 to the caller's IDs even when ownership is
enabled. stat() therefore cannot qualify the on-disk ownership of this fixture.
This narrow checker reads checksummed, variable-key filesystem-tree leaves and
requires all retained versions of each live inode to have the expected owners.
It is not an APFS recovery or general filesystem parser.
"""
import struct


def fletcher(block):
    a = b = 0
    mod = 0xffffffff
    for word in struct.unpack('<1022I', block[8:]):
        a = (a + word) % mod
        b = (b + a) % mod
    x = mod - (a+b) % mod
    return ((mod-(a+x) % mod) << 32) | x


def inode_owners(blocks):
    records = {}
    for block in blocks:
        if len(block) != 4096:
            raise ValueError('Partial APFS block')
        kind, subtype = struct.unpack_from('<II', block, 24)
        if kind & 0xffff not in (2, 3) or subtype != 14:
            continue
        if fletcher(block) != struct.unpack_from('<Q', block)[0]:
            continue  # free space can contain stale, invalid node fragments
        flags, level, count = struct.unpack_from('<HHI', block, 32)
        table_offset, table_size = struct.unpack_from('<HH', block, 40)
        if level != 0:
            continue
        if flags & 4 or count*8 > table_size or 56+table_offset+table_size >= 4096:
            raise ValueError('Unsupported APFS filesystem leaf layout')
        key_base = 56+table_offset+table_size
        value_end = 4096-(40 if flags & 1 else 0)
        for i in range(count):
            ko, kl, vo, vl = struct.unpack_from('<4H', block, 56+table_offset+i*8)
            kp, vp = key_base+ko, value_end-vo
            if kl < 8 or kp+kl > 4096 or vp < key_base or vp+vl > value_end:
                raise ValueError('APFS leaf record outside data area')
            key = struct.unpack_from('<Q', block, kp)[0]
            if key >> 60 != 3:
                continue
            if kl != 8 or vl < 92:
                raise ValueError('Unsupported APFS inode record')
            inode = key & ((1 << 60)-1)
            owner = struct.unpack_from('<II', block, vp+72)
            mode = struct.unpack_from('<H', block, vp+80)[0]
            if mode & 0xf000 not in (0x4000, 0x8000, 0xa000):
                raise ValueError('Unexpected APFS inode type')
            records.setdefault(inode, set()).add(owner)
    return records


def verify_owners(records, live_inodes, profile):
    policy = profile['raw_ownership']
    if set(live_inodes) != {'.', *profile['entries']}:
        raise ValueError('Live inode path set differs from fixture')
    if live_inodes['.'] != 2 or len(set(live_inodes.values())) != len(live_inodes):
        raise ValueError('Unexpected root or duplicate live inode identities')
    for name, inode in live_inodes.items():
        expected = tuple(policy['root'] if name == '.' else policy['children'])
        if records.get(inode) != {expected}:
            raise ValueError('Raw APFS owner mismatch: '+name+' = '+str(records.get(inode)))
    return len(live_inodes)


def audit_image(path, live_inodes, profile):
    with path.open('rb') as source:
        records = inode_owners(iter(lambda: source.read(4096), b''))
    return verify_owners(records, live_inodes, profile)
