#!/usr/bin/env python3
"""Locate a file's data offset inside the raw HFS image by two anchors.
Usage: python3 locate_bin_win.py FILE.bin
Prints IMG_START=<hex> and byte-match status.
"""
import sys
IMG = r'C:\Users\Cisco He\AppData\Local\Temp\iosroot_hfs.raw'
ld = open(sys.argv[1], 'rb').read()

def pick(off):
    best = None
    for o in range(off, min(off + 0x20000, len(ld) - 32), 8):
        w = ld[o:o+32]
        s = len(set(w))
        if best is None or s > best[0]:
            best = (s, o, w)
    return best[1], best[2]

a1_off, a1 = pick(0x400)
a2_off, a2 = pick(max(0x400, len(ld)//2))
f = open(IMG, 'rb'); CH = 64 << 20; pos = 0; hits = []
while True:
    b = f.read(CH)
    if not b:
        break
    i = b.find(a1)
    while i >= 0:
        c = pos + i - a1_off
        if c >= 0:
            hits.append(c)
        i = b.find(a1, i + 1)
    pos += len(b)
good = []
for s in hits:
    f.seek(s + a2_off)
    if f.read(32) == a2:
        good.append(s)
print('size', len(ld), 'hits', len(hits), 'verified', [hex(x) for x in good])
if len(good) != 1:
    sys.exit('need exactly one start')
f.seek(good[0])
print('IMG_START', hex(good[0]))
print('file matches:', f.read(len(ld)) == ld)
