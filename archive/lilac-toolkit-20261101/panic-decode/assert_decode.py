#!/usr/bin/env python3
"""Decode assert identity from two PON measurements.

v114 final PON 0x52 -> assert (file,line) hash &0x3f == 0x12 (or non-assert).
v115 final PON 0xC0|x -> identified assert, x == line & 0x3f.

Usage: assert_decode.py <assert_enum_52.txt> <v115 pon byte hex>
"""
import re, sys

path, pon = sys.argv[1], int(sys.argv[2], 16)
skip = ('iokit/Tests', 'osfmk/i386', 'osfmk/x86_64', 'pexpert/i386',
        'libkern/kxld/tests', 'tests/', 'san/', 'osfmk/kdp/')

def h_assert(basename, line):
    h = 0
    for ch in basename.encode()[:24]:
        h = (h * 33 + ch) & 0xffffffff
    h = (h * 33 + (line & 0xff)) & 0xffffffff
    h = (h * 33 + ((line >> 8) & 0xff)) & 0xffffffff
    return h & 0x3f

if pon < 0xC0:
    print(f"PON 0x{pon:02x} is not an identified assert (0x40-0x7f = plain panic string hash)")
    sys.exit(1)

want = pon & 0x3f
hits = []
for ln in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'([^:]+):(\d+): \[(assertf?)\]', ln)
    if not m:
        continue
    f, line = m.group(1), int(m.group(2))
    if any(f.startswith(s) for s in skip):
        continue
    base = f.rsplit('/', 1)[-1]
    if h_assert(base, line) == 0x12 and (line & 0x3f) == want:
        hits.append((f, line, m.group(3)))
print(f"PON 0x{pon:02x} -> assert hash 0x12 & line%64 = 0x{want:02x}: {len(hits)} candidate(s)")
for f, line, kind in hits:
    print(f"  {f}:{line} [{kind}]")
