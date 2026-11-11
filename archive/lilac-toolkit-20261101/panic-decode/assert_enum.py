#!/usr/bin/env python3
"""Enumerate assert call sites matching a PON assert-hash byte.

Kernel formula (osfmk/kern/debug.c sweet_panic_assert_hash):
  h = 0
  for i < 24 and afile[i]: h = h*33 + (unsigned char)afile[i]   # afile = __FILE_NAME__
  h = h*33 + (line & 0xff)
  h = h*33 + ((line >> 8) & 0xff)
  PON = 0x40 | (h & 0x3f)

Usage: assert_enum.py <xnu root> <pon byte in hex>
"""
import os, re, sys

EXT = ('.c', '.h', '.cpp', '.mm', '.cc')
PAT = re.compile(r'\b(assertf|assert)\s*\(')


def h_assert(basename, line):
    h = 0
    for i, ch in enumerate(basename.encode('utf-8', 'surrogateescape')[:24]):
        h = (h * 33 + ch) & 0xffffffff
    h = (h * 33 + (line & 0xff)) & 0xffffffff
    h = (h * 33 + ((line >> 8) & 0xff)) & 0xffffffff
    return h & 0x3f


def main():
    root, pon = sys.argv[1], int(sys.argv[2], 16)
    want = pon & 0x3f
    hits = []
    for dirpath, dirnames, files in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'build', 'fakeroot')]
        for fn in files:
            if not fn.endswith(EXT):
                continue
            p = os.path.join(dirpath, fn)
            try:
                src = open(p, encoding='utf-8', errors='surrogateescape').read()
            except OSError:
                continue
            for m in PAT.finditer(src):
                line = src.count('\n', 0, m.start()) + 1
                if h_assert(fn, line) != want:
                    continue
                # context: rest of the statement on one line, collapsed
                ls = src.rfind('\n', 0, m.start()) + 1
                le = src.find('\n', m.start())
                if le < 0:
                    le = len(src)
                snippet = ' '.join(src[ls:le].split())[:110]
                rel = os.path.relpath(p, root)
                hits.append((rel, line, m.group(1), snippet))
    print(f"PON 0x{pon:02x} -> assert hash {want:02x}, {len(hits)} call site(s)")
    for rel, line, kind, snip in hits:
        print(f"{rel}:{line}: [{kind}] {snip}")


if __name__ == '__main__':
    main()
