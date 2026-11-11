#!/usr/bin/env python3
"""Locate launchd's file data offset in the raw HFS image (local scan) and
emit one-byte patch offsets for <key>RequireSuccess</key> -> unknown key.
Run on Windows: python3 locate_launchd_win.py
"""
import sys, struct

IMG = r'C:\Users\Cisco He\AppData\Local\Temp\iosroot_hfs.raw'
LD = r'C:\Users\Cisco He\AppData\Local\Temp\launchd.bin'

ld = open(LD, 'rb').read()

# pick two high-entropy 32-byte anchors from code regions
def pick(off):
    best = None
    for o in range(off, off + 0x20000, 0x10):
        w = ld[o:o+32]
        if len(w) < 32:
            break
        score = len(set(w))
        if best is None or score > best[0]:
            best = (score, o, w)
    return best[1], best[2]

a1_off, a1 = pick(0x1000)
a2_off, a2 = pick(0x40000)
print('anchors at', hex(a1_off), hex(a2_off), 'distinct', len(set(a1)), len(set(a2)))

f = open(IMG, 'rb')
CH = 64 << 20
pos = 0
starts = []
while True:
    buf = f.read(CH)
    if not buf:
        break
    i = buf.find(a1)
    while i >= 0:
        cand = pos + i - a1_off
        if cand >= 0:
            starts.append(cand)
        i = buf.find(a1, i + 1)
    pos += len(buf)
print('anchor1 hits:', len(starts))

good = []
for s in starts:
    f.seek(s + a2_off)
    if f.read(32) == a2:
        good.append(s)
print('verified starts:', [hex(x) for x in good])
if len(good) != 1:
    sys.exit('need exactly one verified file start')

start = good[0]
f.seek(start)
print('image copy == launchd.bin:', f.read(len(ld)) == ld)

occ = []
p = 0
while True:
    j = ld.find(b'RequireSuccess', p)
    if j < 0:
        break
    occ.append(j)
    p = j + 1
print('RequireSuccess occurrences:', len(occ))
for j in occ:
    print(hex(start + j), ld[j-8:j+16])
print('--- patch commands (flip last s -> x) ---')
print('IMG_START', hex(start))
for j in occ:
    print(f"printf 'x' | dd of=/dev/block/sda66 bs=1 seek={start + j + 13} conv=notrunc")
