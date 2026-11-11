#!/usr/bin/env python3
"""Replace the ramdisk (SWEETXNU payload) inside an Android boot image.

Layout of the images we boot (Android v0, page size 4096):
  header page (4096) | kernel (gz+dtb, padded) | ramdisk (payload, padded)
There is no separate tail: trailing bytes after the padded ramdisk are zero
padding only. The header's ramdisk_size at 0x10 is rewritten.

usage: repack_boot.py CONTAINER_IMG PAYLOAD OUT
"""
import struct
import sys


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    cont, payload_path, out = sys.argv[1:]
    d = bytearray(open(cont, 'rb').read())
    if d[:8] != b'ANDROID!':
        print('container is not an Android boot image')
        return 1
    ks = struct.unpack_from('<I', d, 0x08)[0]
    rs = struct.unpack_from('<I', d, 0x10)[0]
    ps = struct.unpack_from('<I', d, 0x24)[0]
    if ps != 4096:
        print('unexpected page size', ps)
        return 1
    payload = open(payload_path, 'rb').read()
    kpad = (ks + ps - 1) // ps * ps
    rpad = (rs + ps - 1) // ps * ps
    kernel = d[ps:ps + ks]
    tail = bytes(d[ps + kpad + rpad:])
    if any(tail):
        print('non-zero tail after ramdisk, refusing')
        return 1
    hdr = bytearray(d[:ps])
    struct.pack_into('<I', hdr, 0x10, len(payload))
    npad = (len(payload) + ps - 1) // ps * ps
    outd = bytes(hdr) + kernel + b'\0' * (kpad - ks) + payload + b'\0' * (npad - len(payload))
    open(out, 'wb').write(outd)
    print('kernel=%d payload=%d total=%d ramdisk_off=%d' % (ks, len(payload), len(outd), ps + kpad))
    return 0


if __name__ == '__main__':
    sys.exit(main())
