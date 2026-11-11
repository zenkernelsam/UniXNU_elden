#!/usr/bin/env python3
"""Decode a PON byte 0x40-0x7f written by sweet_early_phase() back to the
kernel_bootstrap_log()/kernel_bootstrap_thread_log() message that produced it.

Usage: python3 phase_hash.py <path-to-startup.c> [hex-byte]
"""
import re, sys

def h64(text):
    h = 0
    for ch in text.encode():
        h = (h * 33 + ch) & 0xffffffff
    return 0x40 | (h & 0x3f)

def main():
    src = open(sys.argv[1], encoding="utf-8", errors="surrogateescape").read()
    table = {}
    for m in re.finditer(r'kernel_bootstrap(?:_thread)?_log\s*\(\s*"([^"\\]*)"\s*\)', src):
        msg = m.group(1)
        table.setdefault(h64(msg), []).append(msg)
    if len(sys.argv) > 2:
        want = int(sys.argv[2], 16) & 0x3f
        hits = table.get(0x40 | want, [])
        print(f"byte 0x{0x40|want:02x}: {hits if hits else 'NO MATCH (not a bootstrap log)'}")
        return
    for k in sorted(table):
        print(f"0x{k:02x}  {', '.join(table[k])}")

if __name__ == "__main__":
    main()
