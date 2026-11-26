"""Audit the metal2vulkan-rt FFI migration gap for the AdrenoMetal bridge.

Parses drivers/AdrenoMetal/bridge/NativeMetalObjects.rs (prepared tree, merged
cc01fa33 content) for its imported metal2vulkan / metal2vulkan_runtime symbols,
then resolves each symbol against the pinned 779eb522 baseline and against
metal2vulkan-development HEAD (the rewritten metal2vulkan-runtime), emitting a
machine-readable m2v-rt-gap.json: per symbol, where it is defined at the pin,
whether it survives at HEAD at the same path, moved, renamed or missing.

Offline static evidence only; no build, no device, no migration decision.

Resolution model (documented approximation): a symbol is "defined" where a
`pub struct|enum|fn|type|const|trait|union` of that name appears in the crate;
its module path is file-derived (src/foo/bar.rs -> foo::bar). Crate-root imports
are preserved when the name is publicly defined anywhere in the same crate.
Renamed candidates are missing names whose lowercase/underscore-normalized form
or Levenshtein distance <= 2 matches a surviving public item.
"""
from pathlib import Path
import argparse, json, re, subprocess

PIN = '779eb522b45615ab7a2e96f823502715238e472b'
ITEM = re.compile(r'^\s*pub(?:\([^)]*\))?\s+(?:async\s+|unsafe\s+|extern\s+"C"\s+)*(struct|enum|fn|type|const|trait|union|static)\s+(\w+)', re.M)
USE = re.compile(r'use\s+(metal2vulkan\w*)::([^;]+);')

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo)] + list(args))

def crate_files(repo, ref, crate):
    out = git(repo, 'ls-tree', '-r', '--name-only', ref, 'crates/' + crate.replace('_', '-') + '/src/').decode()
    return [line for line in out.splitlines() if line.endswith('.rs')]

def module_of(path, crate):
    # crates/<crate>/src/foo/bar.rs -> foo::bar ; src/lib.rs|main.rs|mod.rs handled
    rel = path.split('/src/', 1)[1][:-3]
    parts = [p for p in rel.split('/') if p not in ('lib', 'main')]
    if parts and parts[-1] == 'mod':
        parts.pop()
    return '::'.join(parts)

REEXPORT = re.compile(r'^\s*pub\s+use\s+([\w:]+?)(?:::\{([^}]+)\})?\s*;', re.M)

GLOB = re.compile(r'^\s*pub\s+use\s+([\w:]+)::\*\s*;', re.M)

def pub_index(repo, ref, crate):
    index = {}
    globs = []
    try:
        files = crate_files(repo, ref, crate)
    except subprocess.CalledProcessError:
        return index, False
    for f in files:
        text = git(repo, 'show', ref + ':' + f).decode(errors='replace')
        mod = module_of(f, crate)
        for kind, name in ITEM.findall(text):
            index.setdefault(name, []).append({'module': mod, 'kind': kind, 'file': f})
        # Macro-generated Metal API types: metal_enum!/metal_flags! define a pub type whose
        # name is the first `Name = "MTL..."` binding inside the macro block.
        for mac in re.finditer(r'(?m)^\s*(metal_enum|metal_flags)!\s*\{', text):
            window = text[mac.end():mac.end() + 600]
            m = re.search(r'(\w+)\s*=\s*"', window)
            if m:
                index.setdefault(m.group(1), []).append({'module': mod, 'kind': 'macro_' + mac.group(1), 'file': f})
        for path, braces in REEXPORT.findall(text):
            names = [n.strip().split(' as ')[-1] for n in braces.split(',') if n.strip()] if braces else [path.split('::')[-1]]
            for name in names:
                if name and name != 'self':
                    index.setdefault(name, []).append({'module': mod, 'kind': 'reexport', 'file': f, 'source_path': path})
        for target in GLOB.findall(text):
            globs.append({'module': mod, 'target': target})
    return {'items': index, 'globs': globs}, True

def parse_imports(source):
    imports = []
    for crate, rest in USE.findall(source):
        rest = rest.strip()
        if rest.endswith('}'):
            if rest.startswith('{'):
                path_part, braces = '', rest[1:]
            else:
                path_part, _, braces = rest.rpartition('::{')
            symbols = [s.strip().split(' as ')[0] for s in braces[:-1].split(',') if s.strip()]
            for sym in symbols:
                imports.append({'crate': crate, 'module': path_part, 'symbol': sym})
        else:
            parts = rest.split('::')
            imports.append({'crate': crate, 'module': '::'.join(parts[:-1]), 'symbol': parts[-1].split(' as ')[0]})
    return imports

def lev(a, b):
    if abs(len(a) - len(b)) > 2:
        return 3
    row = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        prev, row[0] = row[0], i
        for j, cb in enumerate(b, 1):
            cur = row[j]
            row[j] = min(row[j] + 1, row[j - 1] + 1, prev + (ca != cb))
            prev = cur
    return row[-1]

def norm(name):
    return name.lower().replace('_', '')

def classify(symbol, module, idx):
    index = idx['items']
    globs = idx['globs']
    if symbol in index:
        mods = [e['module'] for e in index[symbol]]
        exact = module in mods or not module  # crate-root imports count as preserved at any definition site
        if not exact:
            # A glob re-export `pub use ...::<seg>::*` in the imported module exposes items
            # defined in the submodule whose final segment matches.
            for definition_mod in mods:
                seg = definition_mod.split('::')[-1]
                for g in globs:
                    if g['module'] == module and g['target'].split('::')[-1] == seg:
                        exact = True
        return {'status': 'preserved' if exact else 'moved', 'defined_at': index[symbol]}
    candidates = []
    for name, entries in index.items():
        if norm(name) == norm(symbol) or lev(name, symbol) <= 2:
            candidates.append({'name': name, 'defined_at': entries})
    return {'status': 'renamed_candidate' if candidates else 'missing', 'rename_candidates': candidates}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True, help='Prepared tree holding drivers/AdrenoMetal/bridge/NativeMetalObjects.rs')
    p.add_argument('--metal2vulkan', type=Path, required=True, help='metal2vulkan-development clone (read-only)')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    src = (a.tree / 'drivers/AdrenoMetal/bridge/NativeMetalObjects.rs').read_text(errors='replace')
    imports = parse_imports(src)
    crates = sorted({i['crate'] for i in imports})
    repo = a.metal2vulkan.resolve()
    head = git(repo, 'rev-parse', 'HEAD').decode().strip()
    indexes = {}
    for crate in crates:
        indexes[(crate, 'pin')], pin_exists = pub_index(repo, PIN, crate)
        indexes[(crate, 'head')], head_exists = pub_index(repo, head, crate)
    rows = []
    counts = {'total': 0, 'pin_preserved': 0, 'head_preserved': 0, 'head_moved': 0,
              'head_renamed_candidate': 0, 'head_missing': 0}
    for imp in sorted(imports, key=lambda i: (i['crate'], i['module'], i['symbol'])):
        counts['total'] += 1
        pinr = classify(imp['symbol'], imp['module'], indexes[(imp['crate'], 'pin')])
        headr = classify(imp['symbol'], imp['module'], indexes[(imp['crate'], 'head')])
        if pinr['status'] == 'preserved':
            counts['pin_preserved'] += 1
        counts['head_' + ('renamed_candidate' if headr['status'] == 'renamed_candidate' else headr['status'])] += 1
        rows.append({'crate': imp['crate'], 'module': imp['module'], 'symbol': imp['symbol'],
                     'at_pin': pinr, 'at_head': headr})
    crates_exist = {c: {'pin': bool(indexes[(c, 'pin')]['items']), 'head': bool(indexes[(c, 'head')]['items'])} for c in crates}
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'pin': PIN, 'head': head, 'crates_exist': crates_exist, 'counts': counts, 'symbols': rows,
              'resolution_model': 'file-derived module paths; crate-root imports preserved at any pub definition; rename = normalized equality or Levenshtein<=2',
              'device_access': False, 'migration_decision_made': False,
              'note': 'Static FFI surface evidence for a future metal2vulkan-rt migration; no migration is decided or performed here.'}
    (out / 'm2v-rt-gap.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(counts))
    missing = counts['head_missing']
    if missing:
        print('NOTE: %d imported symbols missing at metal2vulkan-development HEAD' % missing)
if __name__ == '__main__':
    main()
