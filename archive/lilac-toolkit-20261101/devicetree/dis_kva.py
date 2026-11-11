#!/usr/bin/env python3
"""Disassemble ranges of k0146.macho (aarch64), VA = fileoff (image base 0xA0804000)."""
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

path = sys.argv[1]
starts = [int(x, 0) for x in sys.argv[2:]]
with open(path, "rb") as f:
    data = f.read()

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True

for start in starts:
    print(f"=== {start:#x} (PA {start + 0xA0804000:#x}) ===")
    end = start + 0x100
    for ins in md.disasm(data[start:end], start):
        print(f"{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")
