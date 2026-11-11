#!/usr/bin/env python3
"""Assemble an Android boot v0 image from a container header, a new
gz-dtb kernel (shim Image.gz-dtb) and a new ramdisk (SWEETXNU payload).

usage: repack_kernel_boot.py CONTAINER KERNEL_GZDTB PAYLOAD OUT
"""
import hashlib
import struct
import sys


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    cont, kern, pay, out = sys.argv[1:]
    d = bytearray(open(cont, 'rb').read())
    if d[:8] != b'ANDROID!':
        print('container is not an Android boot image')
        return 1
    ks, rs = struct.unpack_from('<I', d, 0x08)[0], struct.unpack_from('<I', d, 0x10)[0]
    ss = struct.unpack_from('<I', d, 0x18)[0]
    ps = struct.unpack_from('<I', d, 0x24)[0]
    if ps != 4096:
        print('unexpected page size', ps)
        return 1
    if ss != 0:
        print('container has second stage', ss)
        return 1
    kernel = open(kern, 'rb').read()
    payload = open(pay, 'rb').read()
    hdr = bytearray(d[:ps])
    struct.pack_into('<I', hdr, 0x08, len(kernel))
    struct.pack_into('<I', hdr, 0x10, len(payload))
    struct.pack_into('<I', hdr, 0x18, 0)
    body = bytes(hdr)
    body += kernel + b'\0' * (-len(kernel) % ps)
    body += payload + b'\0' * (-len(payload) % ps)
    open(out, 'wb').write(body)
    print('kernel=%d payload=%d total=%d ramdisk_off=%d' %
          (len(kernel), len(payload), len(body),
           ps + len(kernel) + (-len(kernel) % ps)))
    print('sha256', hashlib.sha256(body).hexdigest()[:16])
    return 0


if __name__ == '__main__':
    sys.exit(main())
