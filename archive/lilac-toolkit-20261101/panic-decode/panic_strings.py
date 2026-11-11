#!/usr/bin/env python3
"""For each panic call-site return address, run llvm-objdump on macOS to find
the x0 format-string load and print the string."""
import re, struct, subprocess, sys

KERNEL = sys.argv[1]
RAS = [int(x, 16) for x in sys.argv[2:]]

def load_segments(path):
    buf = open(path, 'rb').read()
    ncmds = struct.unpack_from('<I', buf, 16)[0]
    off = 32
    segs = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', buf, off)
        if cmd == 0x19:
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<4Q', buf, off + 24)
            segs.append((vmaddr, vmsize, fileoff, filesize))
        off += size
    return buf, segs

BUF, SEGS = load_segments(KERNEL)

def read_str(va, n=120):
    for vm, vs, fo, fs in SEGS:
        if vm <= va < vm + vs:
            o = fo + (va - vm)
            return BUF[o:o + n].split(b'\0')[0].decode(errors='replace')
    return '<unmapped>'

def fmt_for(ra):
    start = ra - 0x48
    out = subprocess.run(['xcrun', 'llvm-objdump', '-d',
                          '--start-address=0x%x' % start,
                          '--stop-address=0x%x' % ra, KERNEL],
                         capture_output=True, text=True).stdout
    base = None
    for line in out.splitlines():
        m = re.search(r'adrp\s+x0, (0x[0-9a-f]+)', line)
        if m:
            base = int(m.group(1), 16)
        m = re.search(r'add\s+x0, x0, #(0x[0-9a-f]+)', line)
        if m and base is not None:
            return read_str(base + int(m.group(1), 16))
    return '<no x0 load found>'

for ra in RAS:
    try:
        print('0x%x  %r' % (ra, fmt_for(ra)))
    except Exception as e:
        print('0x%x  ERROR %s' % (ra, e))
