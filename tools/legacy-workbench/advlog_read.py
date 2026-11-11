#!/usr/bin/env python3
# advlog_read.py — parse a dumped Mu AdvancedLogger memory blob to text.
#
# Two input modes:
#   advlog_read.py info.bin            # file starts AT ADVANCED_LOGGER_INFO ('ALOG')
#   advlog_read.py ptr.bin --ptr       # file starts AT ADVANCED_LOGGER_PTR ('Alog_Ptr');
#                                      # prints the LogBuffer (INFO) physical address, then
#                                      # parses messages if the INFO body is inside the dump.
#
# TWRP/Android dump recipe (device crashed on DXE dump -> warm reset):
#   dd if=/dev/mem of=/tmp/ptr.bin bs=16 skip=$((0xC2000000/16)) count=1   # PTR(16B)
#   # -> bytes 0..7 'Alog_Ptr' sig, bytes 8..15 = INFO physical address INFO_PA
#   dd if=/dev/mem of=/tmp/info.bin bs=4096 skip=$((INFO_PA/4096)) count=2048  # ~8MB max
#   python3 advlog_read.py info.bin
# (skip must be integer in bs units; INFO_PA is 4K-aligned by AllocateReservedPages)
import struct, sys

PHASE = {1: "SEC", 2: "PEI", 3: "PEI64", 4: "DXE", 5: "RT", 6: "MM", 7: "BDS", 8: "UEFI"}


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u64(d, o):
    return struct.unpack_from("<Q", d, o)[0]


def parse_info(d):
    """Parse blob whose offset 0 is ADVANCED_LOGGER_INFO."""
    if len(d) < 0x58 or d[0:4] != b"ALOG":
        print("[!] not an ADVANCED_LOGGER_INFO blob (sig=%r)" % d[0:4])
        return
    ver = u16(d, 4)
    off = u32(d, 0x0C)
    cur = u32(d, 0x14)
    size = u32(d, 0x1C)
    new = u64(d, 0x50)
    print("[+] INFO ver=%d buf_off=0x%x cur_off=0x%x buf_size=0x%x (%d bytes used)"
          % (ver, off, cur, size, max(0, cur - off)))
    if new:
        print("[+] NewLoggerInfoAddress=0x%x (buffer migrated; dump that PA too)" % new)

    end = min(len(d), cur if cur > off else len(d))
    p, n = off, 0
    while p + 24 <= end:
        sig = d[p:p + 4]
        if sig == b"ALM2":
            level = u32(d, p + 6)
            ts = u64(d, p + 10)
            ph = u16(d, p + 18)
            mlen = u16(d, p + 20)
            moff = u16(d, p + 22)
            text = d[p + moff:p + moff + mlen].decode("ascii", "replace").rstrip("\0\r\n")
            print("[%6s] lvl%02x %s" % (PHASE.get(ph, str(ph)), level, text))
            p += ((moff + mlen + 7) & ~7)
        elif sig == b"ALMS":
            level = u32(d, p + 4)
            ts = u64(d, p + 8)
            mlen = u16(d, p + 16)
            text = d[p + 18:p + 18 + mlen].decode("ascii", "replace").rstrip("\0\r\n")
            print("[  ?  ] lvl%02x %s" % (level, text))
            p += ((18 + mlen + 7) & ~7)
        else:
            # resync: find next entry sig
            nxt = d.find(b"ALM", p + 4)
            if nxt < 0 or nxt >= end:
                break
            p = nxt
        n += 1
    print("[+] %d message(s) decoded" % n)


def parse_ptr(d):
    """Parse blob whose offset 0 is ADVANCED_LOGGER_PTR."""
    if d[0:8] != b"Alog_Ptr":
        print("[!] bad PTR signature %r" % d[0:8])
        return None
    info = u64(d, 8)
    print("[+] PTR ok -> LogBuffer(INFO) @ 0x%x" % info)
    return info


if __name__ == "__main__":
    path = sys.argv[1]
    d = open(path, "rb").read()
    if "--ptr" in sys.argv:
        info = parse_ptr(d)
        if info and len(d) > 16:
            # the caller may have dumped more than 16B; INFO lives at PA 'info'.
            # if dump base == PTR address, INFO offset inside file is unknown
            # unless caller dumped a range containing it. Try self-heal:
            off = d.find(b"ALOG", 16)
            if off > 0:
                print("[+] INFO found inside dump @file+0x%x" % off)
                parse_info(d[off:])
    else:
        parse_info(d)
