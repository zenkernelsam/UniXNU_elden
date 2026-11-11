#!/usr/bin/env python3
"""Minimal FDT parser: dump properties of nodes whose path/name matches a
keyword, plus any property containing a phandle reference needed later."""
import struct
import sys

PATH = sys.argv[1]
KEY = sys.argv[2].lower() if len(sys.argv) > 2 else 'gpu'
d = open(PATH, 'rb').read()

magic, totalsize, off_dt_struct, off_dt_strings, off_mem_rsvmap, version, \
    last_comp, boot_cpuid, size_dt_strings, size_dt_struct = struct.unpack(
        '>10I', d[:40])
assert magic == 0xD00DFEED, hex(magic)
ss = d[off_dt_strings:off_dt_strings + size_dt_strings]
st = d[off_dt_struct:off_dt_struct + size_dt_struct]

pos = 0
path = []
props = {}
allnodes = []


def align4(p):
    return (p + 3) & ~3


while pos < len(st):
    tok = struct.unpack_from('>I', st, pos)[0]
    pos += 4
    if tok == 1:  # BEGIN_NODE
        end = st.index(b'\0', pos)
        name = st[pos:end].decode()
        pos = align4(end + 1)
        path.append(name)
    elif tok == 2:  # END_NODE
        p = '/'.join(path)
        allnodes.append((p, props.copy()))
        props.clear()
        if path:
            path.pop()
    elif tok == 3:  # PROP
        ln, nameoff = struct.unpack_from('>II', st, pos)
        pos += 8
        val = st[pos:pos + ln]
        pos = align4(pos + ln)
        end = ss.index(b'\0', nameoff)
        props[ss[nameoff:end].decode()] = val
    elif tok == 4:  # NOP
        pass
    elif tok == 9:
        break
    else:
        break

for p, pr in allnodes:
    if KEY in p.lower() or any(KEY in k.lower() for k in pr):
        print('==', p)
        for k, v in pr.items():
            if len(v) <= 64:
                print('  ', k, '=', v.hex(), repr(v))
            else:
                print('  ', k, '= <%d bytes>' % len(v))
