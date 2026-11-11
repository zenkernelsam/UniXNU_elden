#!/usr/bin/env python3
"""Patch LUN4 GPT to add a `unixnu` partition in the free tail — non-destructive.

elden LUN4 layout (Backup B, 4096B sectors):
  usable LBA 6..1048570; entries end at 425235; free tail 425236..1048570
  (623335 sec = 2435 MiB). Backup GPT entries @1048571..1048574, header @1048575.

This tool ONLY edits GPT headers/entries. It never touches any existing
partition's data. Android ignores unknown partition names, so `unixnu`
coexists with stock Android + personal data unchanged.

Usage:
  python make_unixnu_part.py --dry            # preview against Backup B files
  python make_unixnu_part.py --make           # emit patched sector images + dd cmds
"""
import argparse, os, struct, uuid, zlib, sys

SECTOR = 4096
ENTRIES = 128
ENT_SZ = 128
LUN4_LAST_LBA = 1048575          # backup GPT header lives here
LUN4_BAK_ENT_LBA = 1048571       # backup entry array 1048571..1048574
# unixnu window inside free tail 425236..1048570
UNIXNU_FIRST = 425236
UNIXNU_SECTORS = 524288          # exactly 2 GiB; leaves ~377 MiB tail
PART_NAME = "ARMADA_BACKUP"
# APFS container type GUID — Android ignores it, semantically correct for iPadOS
TYPE_GUID = uuid.UUID("7C3457EF-0000-11AA-AA11-00306543ECAC")
# Deterministic unique GUID (root_match_volume_uuid candidate)
UNIQUE_GUID = uuid.uuid5(uuid.NAMESPACE_DNS, "unixnu.elden.y700")

MAIN_GPT = "Backup/B_edl_stock/lun4/gpt_main4.bin"
BAK_GPT = "Backup/B_edl_stock/lun4/gpt_backup4.bin"
OUTDIR = "edl_work/gpt_armada"


def guid_bytes_le(u):
    return u.bytes_le


def parse_gpt(data, backup=False):
    hdr_off = SECTOR  # sector 1 in the dumped image (dumps start at sector 0)
    if backup:
        # backup dumps: gpt_backupN.bin contains the LAST sectors; header at end
        # find "EFI PART" within last sector
        idx = data.rfind(b"EFI PART")
        if idx < 0:
            raise ValueError("backup GPT signature not found")
        hdr_off = idx
    hdr = bytearray(data[hdr_off:hdr_off + SECTOR])
    if hdr[:8] != b"EFI PART":
        raise ValueError("bad GPT signature")
    fields = {
        "hdr_off": hdr_off,
        "my_lba": struct.unpack("<Q", hdr[24:32])[0],
        "alt_lba": struct.unpack("<Q", hdr[32:40])[0],
        "first_usable": struct.unpack("<Q", hdr[40:48])[0],
        "last_usable": struct.unpack("<Q", hdr[48:56])[0],
        "disk_guid": hdr[56:72],
        "ent_lba": struct.unpack("<Q", hdr[72:80])[0],
        "n_ent": struct.unpack("<I", hdr[80:84])[0],
        "ent_sz": struct.unpack("<I", hdr[84:88])[0],
    }
    # entry array: for main, follows header in dump; for backup, precedes it
    if backup:
        ent_start = hdr_off - ENTRIES * ENT_SZ
        if ent_start < 0:
            # backup dump may only contain header sector(s); fall back to main copy
            raise ValueError("backup dump too small for entry array")
    else:
        ent_start = fields["ent_lba"] * SECTOR
    entries = bytearray(data[ent_start:ent_start + fields["n_ent"] * ENT_SZ])
    return hdr, entries, fields


def add_partition(entries, n_ent):
    for i in range(n_ent):
        e = entries[i * ENT_SZ:(i + 1) * ENT_SZ]
        if len(e) < ENT_SZ:
            break
        if e[:16] != b"\x00" * 16:
            name = e[56:128].decode("utf-16-le", "ignore").rstrip("\x00")
            if name == PART_NAME:
                raise SystemExit(f"partition {PART_NAME} already exists (index {i})")
    for i in range(n_ent):
        e = entries[i * ENT_SZ:(i + 1) * ENT_SZ]
        if e[:16] == b"\x00" * 16:
            new_e = bytearray(ENT_SZ)
            new_e[0:16] = guid_bytes_le(TYPE_GUID)
            new_e[16:32] = guid_bytes_le(UNIQUE_GUID)
            struct.pack_into("<Q", new_e, 32, UNIXNU_FIRST)
            struct.pack_into("<Q", new_e, 40, UNIXNU_FIRST + UNIXNU_SECTORS - 1)
            struct.pack_into("<Q", new_e, 48, 0)  # attrs
            nm = PART_NAME.encode("utf-16-le")
            new_e[56:56 + len(nm)] = nm
            entries[i * ENT_SZ:(i + 1) * ENT_SZ] = new_e
            print(f"  added {PART_NAME} @ index {i}: "
                  f"LBA {UNIXNU_FIRST}..{UNIXNU_FIRST + UNIXNU_SECTORS - 1} "
                  f"({UNIXNU_SECTORS * SECTOR / 2 ** 20:.0f} MiB)")
            print(f"  unique GUID (root_match_volume_uuid): {UNIQUE_GUID}")
            return i
    raise SystemExit("no free GPT entry slot")


def fix_header(hdr):
    # zero CRC, recompute over header_size bytes
    struct.pack_into("<I", hdr, 16, 0)
    hdr_size = struct.unpack("<I", hdr[12:16])[0]
    crc = zlib.crc32(bytes(hdr[:hdr_size])) & 0xFFFFFFFF
    struct.pack_into("<I", hdr, 16, crc)
    return hdr


def fix_entries_crc(hdr, entries):
    crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
    struct.pack_into("<I", hdr, 88, crc)


def sector_blob(sectors):
    """list of (lba, bytes) -> printable dd lines"""
    return sectors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--make", action="store_true")
    ap.add_argument("--main", default=MAIN_GPT)
    ap.add_argument("--backup", default=BAK_GPT)
    args = ap.parse_args()

    main_data = open(args.main, "rb").read()
    hdr, entries, f = parse_gpt(main_data)
    print(f"GPT: usable {f['first_usable']}..{f['last_usable']}, "
          f"entries@{f['ent_lba']} n={f['n_ent']}")
    # show free tail
    used = []
    for i in range(f["n_ent"]):
        e = entries[i * ENT_SZ:(i + 1) * ENT_SZ]
        if len(e) < ENT_SZ or e[:16] == b"\x00" * 16:
            continue
        fl, ll = struct.unpack("<QQ", e[32:48])
        nm = e[56:128].decode("utf-16-le", "ignore").rstrip("\x00")
        used.append((fl, ll, nm))
    used.sort()
    tail_start = used[-1][1] + 1
    print(f"  last entry ends @ {used[-1][1]} ({used[-1][2]}); "
          f"free tail {tail_start}..{f['last_usable']} "
          f"= {(f['last_usable'] - tail_start + 1) * SECTOR / 2 ** 20:.0f} MiB")
    if UNIXNU_FIRST < tail_start:
        raise SystemExit(f"unixnu first {UNIXNU_FIRST} collides: free tail starts {tail_start}")
    if UNIXNU_FIRST + UNIXNU_SECTORS - 1 > f["last_usable"]:
        raise SystemExit("unixnu window exceeds last usable LBA")

    if args.dry:
        print("--dry: no writes. unixnu would occupy "
          f"{UNIXNU_FIRST}..{UNIXNU_FIRST + UNIXNU_SECTORS - 1}")
        return

    add_partition(entries, f["n_ent"])
    fix_entries_crc(hdr, entries)
    fix_header(hdr)

    # backup header: mirror of primary with my_lba/alt_lba swapped and
    # ent_lba pointing at the backup array location
    bak_hdr = bytearray(hdr)
    struct.pack_into("<Q", bak_hdr, 24, LUN4_LAST_LBA)     # my_lba
    struct.pack_into("<Q", bak_hdr, 32, 1)                # alt_lba (primary at 1)
    struct.pack_into("<Q", bak_hdr, 72, LUN4_BAK_ENT_LBA)  # entries start
    fix_header(bak_hdr)

    os.makedirs(OUTDIR, exist_ok=True)
    out = [
        ("gpt_main4_hdr.bin", hdr[:SECTOR], 1, "primary GPT header"),
        ("gpt_main4_ent.bin", bytes(entries), 2, "primary GPT entries"),
        ("gpt_backup4_ent.bin", bytes(entries), LUN4_BAK_ENT_LBA, "backup GPT entries"),
        ("gpt_backup4_hdr.bin", bytes(bak_hdr[:SECTOR]), LUN4_LAST_LBA, "backup GPT header"),
    ]
    for name, blob, lba, desc in out:
        p = os.path.join(OUTDIR, name)
        open(p, "wb").write(blob)
        print(f"  wrote {p} ({len(blob)} B) -> {desc} @LBA{lba}")

    print("\n=== dd commands (run in TWRP shell or Android su) ===")
    print(f"dd if={OUTDIR}/gpt_main4_hdr.bin  of=/dev/block/sdf bs=4096 seek=1")
    print(f"dd if={OUTDIR}/gpt_main4_ent.bin  of=/dev/block/sdf bs=4096 seek=2")
    print(f"dd if={OUTDIR}/gpt_backup4_ent.bin of=/dev/block/sdf bs=4096 seek={LUN4_BAK_ENT_LBA}")
    print(f"dd if={OUTDIR}/gpt_backup4_hdr.bin of=/dev/block/sdf bs=4096 seek={LUN4_LAST_LBA}")
    print("\nNOTE: LUN4 on this device enumerates as /dev/block/sdf (verify with "
          "'sg_disk /dev/block/sdf' or 'fdisk -l'). Sync afterwards: 'sync'.")


if __name__ == "__main__":
    main()
