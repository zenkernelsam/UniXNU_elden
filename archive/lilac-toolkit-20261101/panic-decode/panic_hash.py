#!/usr/bin/env python3
"""Decode a PON panic hash (0x40-0x7f) back to panic() format strings.

The kernel writes 0x40|(hash&0x3f), where hash is
  h = 0; for first 24 chars of the format string: h = h*33 + c  (uint32)
Usage: panic_hash.py <xnu source root> <pon hex byte>
"""
import os, re, sys

def h64(s):
    h = 0
    for c in s.encode('utf-8', 'surrogateescape')[:24]:
        h = (h * 33 + c) & 0xffffffff
    return h & 0x3f

def main():
    root, pon = sys.argv[1], int(sys.argv[2], 16)
    want = pon & 0x3f
    pat = re.compile(r'panic(?:log_append_noflush)?\s*\(\s*"((?:[^"\\]|\\.)*)"')
    hits = []
    for dirpath, _, files in os.walk(root):
        if '/.git' in dirpath:
            continue
        for fn in files:
            if not fn.endswith('.c') and not fn.endswith('.cpp'):
                continue
            p = os.path.join(dirpath, fn)
            try:
                src = open(p, encoding='utf-8', errors='surrogateescape').read()
            except OSError:
                continue
            for m in pat.finditer(src):
                msg = m.group(1)
                # decode C escapes crudely for hashing purposes
                msg = msg.encode().decode('unicode_escape', errors='ignore')
                if h64(msg) == want:
                    line = src.count('\n', 0, m.start()) + 1
                    hits.append(f"{p}:{line}: {msg[:60]}")
    print(f"PON 0x{pon:02x} -> hash {want:02x}, {len(hits)} match(es)")
    for h in hits[:40]:
        print(h)

if __name__ == '__main__':
    main()
