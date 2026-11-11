# GPT attribute surgical fix: restore per-entry attribute bits from Backup B
# healthy state, preserving new entries (ARMADA_BACKUP etc). Fixes QC A/B
# unbootable-flag loop (ffbm-00 -> fastboot).
import minifh, time, struct, zlib, sys

REF4 = 'D:/Desktop/Git/Patch/Y700-Elden/Backup/B_edl_stock/lun4/gpt_main4.bin'
REF0 = 'D:/Desktop/Git/Patch/Y700-Elden/Backup/B_edl_stock/lun0/gpt_main0.bin'

def read_sectors(s, lun, start, n):
    xml = ('<?xml version="1.0" ?><data><read SECTOR_SIZE_IN_BYTES="4096" '
           f'num_partition_sectors="{n}" physical_partition_number="{lun}" '
           f'start_sector="{start}"/></data>')
    s.write(xml.encode() + b'\x00')
    buf = b''; t0 = time.time()
    while time.time() - t0 < 20:
        c = s.read(8192)
        if c:
            buf += c
            i = buf.find(b'EFI PART')
            if i >= 0 and len(buf) - i >= n * 4096:
                # keep draining briefly for the trailing xml so the port stays clean
                time.sleep(0.3)
                try:
                    buf += s.read(8192) or b''
                except Exception:
                    pass
                break
    return buf

def extract_raw(buf, n):
    """raw payload starts at 'EFI PART' (= LBA1 header, our read start)."""
    i = buf.find(b'EFI PART')
    if i >= 0:
        return buf[i:i + n * 4096]
    return b''

def parse_entries(d, off=4096):
    ents = {}
    for i in range(0, len(d) - off, 128):
        e = d[off + i:off + i + 128]
        if len(e) < 128:
            break
        name = e[56:128].decode('utf-16-le', errors='ignore').rstrip('\x00')
        if name:
            ents[name] = e
    return ents

def fix_lun(s, lun, ref_path):
    ref = open(ref_path, 'rb').read()
    ri = ref.find(b'EFI PART')
    # backup file layout: PMBR(sector0) + header@ri + entries@ri+4096
    ref_entries = parse_entries(ref, ri + 4096)

    buf = read_sectors(s, lun, 1, 5)          # header + first 4 entry sectors
    raw = extract_raw(buf, 5)
    if len(raw) < 5 * 4096:
        print(f'LUN{lun}: read short ({len(raw)})'); return False
    live = bytearray(raw)
    live_entries = parse_entries(live, 4096)

    diffs = []
    for name, re in ref_entries.items():
        le = live_entries.get(name)
        if le is None:
            continue
        if le[48:56] != re[48:56]:            # attr field = entry offset 48..56
            diffs.append((name, struct.unpack('<Q', le[48:56])[0],
                          struct.unpack('<Q', re[48:56])[0]))
    for name, old, new in diffs:
        print(f'LUN{lun} {name:24s} attr 0x{old:016x} -> 0x{new:016x}')
    if not diffs:
        print(f'LUN{lun}: attrs already healthy'); return True

    # apply attr fixes (bytes 48..55 = priority/retry/success slot bits)
    for i in range(0, 4 * 4096, 128):
        e = live[4096 + i:4096 + i + 128]
        name = e[56:128].decode('utf-16-le', errors='ignore').rstrip('\x00')
        if name in {d[0] for d in diffs}:
            live[4096 + i + 48:4096 + i + 56] = ref_entries[name][48:56]
            print(f'  patched {name}')

    # fix CRCs: entries CRC at header+0x58, header CRC at +0x10 (zeroed during calc)
    num = struct.unpack('<I', live[0x50:0x54])[0]
    esz = struct.unpack('<I', live[0x54:0x58])[0]
    entries = bytes(live[4096:4096 + num * esz])
    live[0x58:0x5c] = struct.pack('<I', zlib.crc32(entries) & 0xffffffff)
    live[0x10:0x14] = b'\x00' * 4
    live[0x10:0x14] = struct.pack('<I', zlib.crc32(bytes(live[:92])) & 0xffffffff)

    tmp = f'gpt_lun{lun}_fixed.bin'
    open(tmp, 'wb').write(live[:5 * 4096])
    return minifh.program(s, lun, 1, 5, tmp, f'gpt_lun{lun}_attrfix')

if __name__ == '__main__':
    s = minifh.open_port()
    minifh.configure(s)
    ok4 = fix_lun(s, 4, REF4)
    print('LUN4:', ok4)
    ok0 = fix_lun(s, 0, REF0)
    print('LUN0:', ok0)
    xml = ('<?xml version="1.0" ?><data>'
           '<setbootablestoragedrive value="1"/></data>')
    s.write(xml.encode() + b'\x00')
    time.sleep(2)
    print(minifh.read_resp(s))
    # clear ffbm-00 residue in misc then reset
    ok_m = minifh.program(s, 0, 89992, 256, 'misc_zero.bin', 'misc')
    print('misc zero:', ok_m)
    minifh.reset(s)
    s.close()
