#!/usr/bin/env python3
"""Map a PON byte written by the panic caller beacon back to panic() call sites.

The kernel writes ((return_address >> 4) & 0xff) after panic() is entered.
Usage: panic_caller.py KERNEL.macho [pon_hex]
"""
import struct, sys

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2

def parse(path):
    buf = open(path, 'rb').read()
    magic, cputype, cpusub, ftype, ncmds, sizeofcmds = struct.unpack_from('<6I', buf, 0)
    assert magic == 0xfeedfacf
    off = 32
    segs, symtab = [], None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', buf, off)
        if cmd == LC_SEGMENT_64:
            name = buf[off + 8:off + 24].split(b'\0')[0].decode(errors='replace')
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<4Q', buf, off + 24)
            maxprot, initprot = struct.unpack_from('<II', buf, off + 56)
            segs.append(dict(name=name, vmaddr=vmaddr, vmsize=vmsize,
                             fileoff=fileoff, filesize=filesize, initprot=initprot))
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack_from('<4I', buf, off + 8)
        off += size
    syms = []
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        for i in range(nsyms):
            n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from('<IBBHQ', buf, symoff + 16 * i)
            end = buf.find(b'\0', stroff + n_strx)
            name = buf[stroff + n_strx:end].decode(errors='replace')
            if n_type & 0x0e in (0x0e, 0x0f) and n_value:
                syms.append((n_value, name))
    syms.sort()
    return buf, segs, syms

def containing(syms, va):
    lo, hi = 0, len(syms)
    while lo < hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= va:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return ("?", 0)
    addr, name = syms[lo - 1]
    return (name, va - addr)

def main():
    path = sys.argv[1]
    want = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    buf, segs, syms = parse(path)
    panic_va = None
    for a, n in syms:
        if n in ('_panic', 'panic'):
            panic_va = a
            break
    assert panic_va, 'panic symbol not found'
    print(f'panic @ 0x{panic_va:x}, {len(syms)} symbols')
    hits = []
    for sg in segs:
        if not (sg['initprot'] & 0x4) or sg['filesize'] == 0:
            continue
        data = buf[sg['fileoff']:sg['fileoff'] + sg['filesize']]
        for o in range(0, len(data) - 3, 4):
            w, = struct.unpack_from('<I', data, o)
            if (w & 0xFC000000) != 0x94000000:
                continue
            imm = w & 0x03FFFFFF
            if imm & 0x02000000:
                imm -= 0x04000000
            va = sg['vmaddr'] + o
            if va + imm * 4 == panic_va:
                ra = va + 4
                byte = (ra >> 4) & 0xff
                fn, delta = containing(syms, va)
                hits.append((byte, sg['name'], fn, delta, ra))
    hits.sort()
    print(f'{len(hits)} panic() call sites')
    if len(sys.argv) > 3:
        want_hi = int(sys.argv[3], 16)
        print(f'--- matches for lo=0x{want:02x} hi=0x{want_hi:02x} ---')
        for byte, seg, fn, delta, ra in hits:
            if byte == want and ((ra >> 8) & 0xff) == want_hi:
                print(f'{seg:16s} {fn}+0x{delta:x}  ra=0x{ra:x}')
        return
    if want is None:
        for byte, seg, fn, delta, ra in hits:
            print(f'0x{byte:02x}  {seg:16s} {fn}+0x{delta:x}  ra=0x{ra:x}')
    else:
        print(f'--- matches for PON 0x{want:02x} ---')
        for byte, seg, fn, delta, ra in hits:
            if byte == want:
                print(f'{seg:16s} {fn}+0x{delta:x}  ra=0x{ra:x}')

if __name__ == '__main__':
    main()
