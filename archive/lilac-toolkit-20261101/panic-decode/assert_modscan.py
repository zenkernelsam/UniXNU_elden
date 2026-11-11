#!/usr/bin/env python3
"""List every assert-like call site whose __LINE__ mod 4096 == target."""
import os, re, sys

root = os.path.expanduser("~/Desktop/ios-xperia-xz1c/dxb/xnu")
target = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0xa13
PAT = re.compile(r'([A-Za-z_][A-Za-z0-9_]*assert[A-Za-z0-9_]*)\s*\(', re.IGNORECASE)
count = 0
for dp, dn, fs in os.walk(root):
    dn[:] = [d for d in dn if d not in ('.git', 'build', 'fakeroot')]
    for fn in fs:
        if not fn.endswith(('.c', '.h', '.cpp', '.mm', '.cc', '.m', '.hpp')):
            continue
        p = os.path.join(dp, fn)
        try:
            src = open(p, encoding='utf-8', errors='surrogateescape').read()
        except OSError:
            continue
        for m in PAT.finditer(src):
            line = src.count('\n', 0, m.start()) + 1
            if (line & 0xfff) != target:
                continue
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            print(f"{rel}:{line}:{m.group(1)}")
            count += 1
print(f"total {count}")
