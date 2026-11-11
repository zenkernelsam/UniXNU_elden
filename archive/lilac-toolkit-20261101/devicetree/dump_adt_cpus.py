#!/usr/bin/env python3
"""Dump /cpus children and their reg properties from an Apple ADT."""
import sys
sys.path.insert(0, '/Users/ciscohe/Desktop/ios-xperia-xz1c/tools')
import adt

p = sys.argv[1] if len(sys.argv) > 1 else '/private/tmp/adt-v144.bin'
root, _ = adt.parse(open(p, 'rb').read())

def find(node, name):
    for c in node.children:
        if c.name == name:
            return c
    return None

cpus = find(root, 'cpus')
if not cpus:
    raise SystemExit('no /cpus')
print('/cpus children:', len(cpus.children))
for c in cpus.children:
    reg = c.prop('reg')
    state = c.prop('state')
    print(c.name, 'reg=', reg.hex() if reg else None,
          'state=', state.hex() if state else None,
          'props=', [pr[0] for pr in c.props])
