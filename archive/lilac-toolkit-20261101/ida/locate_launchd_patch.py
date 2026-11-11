#!/usr/bin/env python3
"""Locate launchd's in-image file offset and list RequireSuccess patch sites.

Verifies contiguity with a second anchor, then emits TWRP commands that flip
the last 's' of every <key>RequireSuccess</key> to 'x' (unknown key => task
runs but is not fatal). Nothing is written here.
"""
import sys, os

IMG = '/mnt/c/Users/Cisco He/AppData/Local/Temp/iosroot_hfs.raw'
LD = '/root/launchd.bin'
CHUNK = 32 << 20

ld = open(LD, 'rb').read()
a1_off, a1 = 0x1000, ld[0x1000:0x1020]
a2_off, a2 = 0x40000, ld[0x40000:0x40020]
if len(set(a1)) < 4:
    sys.exit('anchor1 not distinctive')

f = open(IMG, 'rb')
pos = 0
hits = []
while True:
    buf = f.read(CHUNK)
    if not buf:
        break
    i = buf.find(a1)
    while i >= 0:
        hits.append(pos + i - a1_off)
        i = buf.find(a1, i + 1)
    pos += len(buf)
print('anchor1 hits:', [hex(h) for h in hits[:8]], 'total', len(hits))

good = []
for start in hits:
    if start < 0:
        continue
    f.seek(start + a2_off)
    if f.read(len(a2)) == a2:
        good.append(start)
print('file start candidates:', [hex(g) for g in good])
if len(good) != 1:
    sys.exit('need exactly one verified start')

start = good[0]
occ = []
p = 0
while True:
    j = ld.find(b'RequireSuccess', p)
    if j < 0:
        break
    occ.append(j)
    p = j + 1
print('RequireSuccess occurrences:', len(occ))
base = start + occ[0]
f.seek(start)
same = f.read(len(ld)) == ld
print('full file matches image copy:', same)
for j in occ:
    f.seek(start + j)
    ctx = f.read(14)
    print(hex(start + j), ctx)
print('--- TWRP patch commands ---')
for j in occ:
    print(f"printf 'x' | dd of=/dev/block/sda66 bs=1 seek={start + j + 13} conv=notrunc")
