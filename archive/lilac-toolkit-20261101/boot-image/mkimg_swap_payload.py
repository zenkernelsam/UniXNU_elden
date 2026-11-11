#!/usr/bin/env python3
"""Swap the SWEETXNU payload inside an existing shim boot image.

Usage: mkimg_v145.py BASE.img PAYLOAD.bin OUT.img
Keeps the exact shim kernel blob (gzip(Image)+dtb) and header template of
BASE, replaces only the ramdisk (the SWEETXNU payload), page-aligns.
"""
import struct, sys

base_p, payload_p, out_p = sys.argv[1:4]
base = open(base_p, 'rb').read()
payload = open(payload_p, 'rb').read()
_, ksize, kaddr, rsize, raddr, second, saddr, tags, page, hv = struct.unpack('<8s9I', base[:44])
assert second == 0 and page == 4096, (second, page)
# v0 layout: magic@0, kernel_size@8, ramdisk_size@0x10, second_size@0x18
kblob = base[page:page + ksize]
assert len(kblob) == ksize
hdr = bytearray(base[:page])
struct.pack_into('<I', hdr, 0x10, len(payload))   # ramdisk_size

out = bytearray(hdr)
out += kblob
out += b'\0' * (-len(out) % page)
out += payload
out += b'\0' * (-len(out) % page)
open(out_p, 'wb').write(out)
print('kernel', ksize, 'ramdisk', len(payload), 'image', len(out))
