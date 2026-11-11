#!/usr/bin/env python3
"""Pull and decode kShimC /chosen diagnostic properties from booted Android.

Usage:  python kshim_dt_read.py [--save-dir DIR]

Reads (via adb shell, base64 to dodge CRLF):
  /sys/firmware/devicetree/base/chosen/kshim,storage-entry-v1
  /sys/firmware/devicetree/base/chosen/kshim,ufs-read-v1
  /sys/firmware/devicetree/base/chosen/kshim,ufs-link-v1
  /sys/firmware/devicetree/base/chosen/kshim,ufs-power-v1

storage-entry-v1 layout (all BE):
  +0  version u32      +4  valid u32       +8  board u32      +12 status u32
  +16 entry_count u32  +20 read_attempts   +24 stream_count  +28 streams_scanned
  +32 matching_streams +36 contexts_captured +40 read_error i32 +44 failed_addr u64
  +52 entries[]: each 12B = {address u64, value u32}

UFS reg names (base 0x1d84000) and SMMU decode notes at bottom.
"""
import base64
import struct
import subprocess
import sys
import os

CHOSEN = "/sys/firmware/devicetree/base/chosen"
ADB = r"D:\Desktop\Git\SchoolBox\Tools\platform-tools_ADB\adb.exe"
SERIAL = "HA2A1PT2"
PROPS = [
    "kshim,storage-entry-v1",
    "kshim,ufs-read-v1",
    "kshim,ufs-link-v1",
    "kshim,ufs-power-v1",
]

UFS_BASE = 0x1D84000
SMMU_BASE = 0x15000000

UFS_NAMES = {
    0x00: "CAP", 0x04: "MCQCAP", 0x08: "VER", 0x0C: "GRHIT?",
    0x18: "AHIT", 0x20: "IS", 0x24: "IE",
    0x30: "HCS", 0x34: "HCE",
    # 0x38-0x4C = UIC error-code bank (JESD223: per-layer), NOT command regs.
    0x38: "UEC?", 0x3C: "UEC_PA", 0x40: "UEC_DL",
    0x44: "UEC_N", 0x48: "UEC_T", 0x4C: "UEC_DME",
    0x50: "UTRLBA_LO", 0x54: "UTRLBA_HI", 0x58: "UTRLDBR",
    0x5C: "UTRLCLR", 0x60: "UTRLRSR", 0x64: "UTRLCNR",
    0x70: "UTMRLBA_LO", 0x74: "UTMRLBA_HI",
    0x78: "UTMRLDBR", 0x7C: "UTMRLCLR", 0x80: "UTMRLRSR",
    0x90: "UICCMD", 0x94: "UICARG1", 0x98: "UICARG2", 0x9C: "UICARG3",
    0xA0: "UICCMDR?",
    0xD8: "VEND_D8?", 0xDC: "VEND_DC?", 0xE0: "VEND_E0?", 0xE4: "VEND_E4?",
    0xFC: "VENDOR?",
    0x300: "MCQ_MEMCFG", 0x284C: "QC_VENDOR_284C",
}

PROBE_STATUS = {
    0: "OK", 1: "NOT_RUN", 2: "BOARD_UNSUPPORTED", 3: "INVALID_ARGUMENT",
    4: "READ_FAILED", 5: "SMMU_UNSUPPORTED", 6: "BOUNDS", 7: "FULL",
    8: "ENV_UNAVAILABLE",
}


def pull_prop(name):
    cmd = [ADB, "-s", SERIAL, "shell", "su", "-c",
           f"cat {CHOSEN}/{name} | base64 -w0 2>/dev/null || "
           f"cat {CHOSEN}/{name} | base64"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if out.returncode != 0 or not out.stdout.strip():
        return None
    try:
        return base64.b64decode(out.stdout.strip())
    except Exception:
        return None


def u32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def u64(b, o):
    return struct.unpack(">Q", b[o:o + 8])[0]


def decode_entry(blob):
    if len(blob) < 52:
        print("  too short")
        return
    ver, valid, board, status = u32(blob, 0), u32(blob, 4), u32(blob, 8), u32(blob, 12)
    ec, ra = u32(blob, 16), u32(blob, 20)
    sc, ss, ms, cc = u32(blob, 24), u32(blob, 28), u32(blob, 32), u32(blob, 36)
    re_ = struct.unpack(">i", blob[40:44])[0]
    fa = u64(blob, 44)
    print(f"  version={ver} valid=0x{valid:x} board={board} "
          f"status={PROBE_STATUS.get(status, status)}")
    print(f"  entries={ec} read_attempts={ra} streams={sc} scanned={ss} "
          f"SID60_matches={ms} ctx_captured={cc}")
    if status != 0:
        print(f"  read_error={re_} failed_address=0x{fa:x}")
    for i in range(min(ec, (len(blob) - 52) // 12)):
        o = 52 + i * 12
        addr, val = u64(blob, o), u32(blob, o + 8)
        if UFS_BASE <= addr < UFS_BASE + 0x3000:
            off = addr - UFS_BASE
            name = UFS_NAMES.get(off, f"off_{off:x}")
            print(f"  UFS  {name:16s} +0x{off:04x} = 0x{val:08x}")
        elif SMMU_BASE <= addr < SMMU_BASE + 0x100000:
            off = addr - SMMU_BASE
            tag = ""
            if off == 0:
                tag = "CR0"
            elif off == 0x20:
                tag = "IDR0"
            elif off == 0x24:
                tag = "IDR1"
            elif off == 0x28:
                tag = "IDR2"
            elif 0x800 <= off < 0xC00:
                tag = f"SMR[{(off - 0x800) // 4}]"
            elif 0xC00 <= off < 0x1000:
                tag = f"S2CR[{(off - 0xC00) // 4}]"
            print(f"  SMMU {tag:10s} +0x{off:05x} = 0x{val:08x}   (abs 0x{addr:x})")
        else:
            print(f"  ?    0x{addr:012x} = 0x{val:08x}")


def decode_read(blob):
    if len(blob) < 164:
        print("  too short")
        return
    ver, board, stage = u32(blob, 0), u32(blob, 4), u32(blob, 8)
    status = struct.unpack(">i", blob[12:16])[0]
    rel = struct.unpack(">i", blob[16:20])[0]
    rv, hs, bs = u32(blob, 20), u32(blob, 24), u32(blob, 28)
    bc, dma = u64(blob, 32), u64(blob, 40)
    fl, ll = u64(blob, 48), u64(blob, 56)
    uuid = blob[64:80].hex()
    hcrc, tcrc = u32(blob, 80), u32(blob, 84)
    prefix = blob[88:120]
    hcs, isr, db = u32(blob, 120), u32(blob, 124), u32(blob, 128)
    scsi, sense, ocs = u32(blob, 132), u32(blob, 136), u32(blob, 140)
    resp_prefix = blob[144:160]
    debug_valid = u32(blob, 160)
    print(f"  version={ver} board={board} stage={stage} status={status} "
          f"release_status={rel}")
    print(f"  restore_verified={rv} handoff_safe={hs}")
    print(f"  block_size={bs} block_count={bc} "
          f"({bc * bs / 1024 ** 3:.1f} GiB)")
    print(f"  dma_base=0x{dma:x}")
    print(f"  backup-GPT part: uuid={uuid} first_lba={fl} last_lba={ll}")
    print(f"  header_crc=0x{hcrc:08x} tail_crc=0x{tcrc:08x}")
    print(f"  header_prefix: {prefix.hex()}")
    print(f"  hcs=0x{hcs:08x} is=0x{isr:08x} doorbell=0x{db:08x}")
    print(f"  scsi_status=0x{scsi:x} sense=0x{sense:x} ocs=0x{ocs:x}")
    print(f"  response_prefix: {resp_prefix.hex()}")
    print(f"  debug_valid=0x{debug_valid:08x}  "
          f"(*captured regs/prefixes meaningful only where bits set)")


def decode_link_state(b, o, tag):
    fields = ["initial_tx", "current_tx", "last_command", "command_result",
              "last_is", "power_status", "phase", "uncertain", "restore_hibern8"]
    vals = {f: u32(b, o + i * 4) for i, f in enumerate(fields)}
    print(f"  {tag}: " + " ".join(f"{f}=0x{v:x}" for f, v in vals.items()))


def decode_link(blob):
    if len(blob) < 76:
        print(f"  too short ({len(blob)} < 76)")
        return
    print(f"  version={u32(blob, 0)}")
    decode_link_state(blob, 4, "before_release")
    decode_link_state(blob, 40, "after_release ")


def decode_power(blob):
    if len(blob) < 100:
        print(f"  too short ({len(blob)} < 100)")
        return
    print(f"  version={u32(blob, 0)}")
    for tag, o in (("power_before", 4), ("power_after", 24)):
        v = [u32(blob, o + i * 4) for i in range(5)]
        print(f"  {tag}: mode 0x{v[0]:x}->0x{v[1]:x} phase={v[2]} "
              f"uncertain={v[3]} restore_mode=0x{v[4]:x}")
    for tag, o in (("scsi_before", 44), ("scsi_after", 72)):
        v = [u32(blob, o + i * 4) for i in range(7)]
        print(f"  {tag}: op=0x{v[0]:x} lun={v[1]} status=0x{v[2]:x} "
              f"sense=0x{v[3]:x} asc=0x{v[4]:x} ascq=0x{v[5]:x} valid={v[6]}")


def main():
    save_dir = None
    if "--save-dir" in sys.argv:
        save_dir = sys.argv[sys.argv.index("--save-dir") + 1]
        os.makedirs(save_dir, exist_ok=True)
    decoders = {
        "kshim,storage-entry-v1": decode_entry,
        "kshim,ufs-read-v1": decode_read,
        "kshim,ufs-link-v1": decode_link,
        "kshim,ufs-power-v1": decode_power,
    }
    any_found = False
    for name in PROPS:
        blob = pull_prop(name)
        print(f"===== {name} =====")
        if blob is None:
            print("  (absent)")
            continue
        any_found = True
        print(f"  {len(blob)} bytes")
        if save_dir:
            path = os.path.join(save_dir, name.replace(",", "_") + ".bin")
            with open(path, "wb") as f:
                f.write(blob)
            print(f"  saved {path}")
        decoders[name](blob)
    if not any_found:
        print("\nNo kshim properties found. Was Android booted through a "
              "probe-enabled kShimC (CONFIG_KSHIM_STORAGE_ENTRY_PROBE)?")


if __name__ == "__main__":
    main()
