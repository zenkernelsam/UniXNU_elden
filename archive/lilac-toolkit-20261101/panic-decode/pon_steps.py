#!/usr/bin/env python3
"""Decode a PON 0x88e byte for the v128/v129 bring-up build.

Usage: pon_steps.py 0x5c
"""
import sys

STEPS = {
    0x40: "128 (pre-spllo area)",
    0x41: "129",
    0x42: "130",
    0x43: "131 PE_init_iokit",
    0x44: "132 after PE_init_iokit",
    0x45: "133 STARTUP_SUB_EARLY_BOOT done",
    0x46: "134 after spllo",
    0x47: "135 pre-commpage",
    0x48: "136 vm_commpage_init done",
    0x49: "137 commpage text init done",
    0x4A: "138 mac_policy_initmach done",
    0x4D: "141 dtrace early done",
    0x4E: "142 csm provisioning done",
    0x4F: "143 static trust cache loaded",
    0x50: "144 STARTUP_SUB_LOCKDOWN init done",
    0x51: "145 OSKextRemoveKextBootstrap done",
    0x57: "151 machine_lockdown done",
    0x58: "152 PE_lockdown_iokit done",
    0x59: "153 vm_set_restrictions done",
    0x5A: "154 kperf_init_early done",
    0x4B: "139 entering bsd_init  <-- goal threshold",
    0x5B: "155 bsd_init mountroot loop entered  <-- waiting for root",
    0x5C: "156 first vfs_mountroot() failure  <-- waiting for root",
    0x56: "150 (old spllo probe marker)",
}


def main():
    v = int(sys.argv[1], 0) & 0xff if len(sys.argv) > 1 else None
    if v is None:
        for k in sorted(STEPS):
            print(f"0x{k:02x} {STEPS[k]}")
        return
    if v in STEPS:
        print(f"PON 0x{v:02x} = step {STEPS[v]}")
    elif 0x80 <= v < 0xC0:
        print(f"PON 0x{v:02x} = PANIC beacon, last step {v & 0x3f}"
              " (DDR cell has the panic string)")
    elif v == 0x52:
        print("PON 0x52: ambiguous - generic assert format hash or step 146 "
              "(which we skip); read the DDR cell")
    else:
        print(f"PON 0x{v:02x}: not a step marker; check DDR cell / ASSERT hash")


if __name__ == '__main__':
    main()
