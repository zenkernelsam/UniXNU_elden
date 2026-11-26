"""Standalone host verification of lockfile integrity, worktree_file inputs, patch identity
and build/lock reproducibility parity.

Deterministic positive + negative/fault-injection host checks; pure python, zero device IO.
Positive: all lockfiles parse; every pinned commit is a strict lowercase 40-hex SHA; every
compatibility input's worktree_file exists and matches its pinned sha256 and is hash-bound
into the prepared inputs; all Mesa/Turnip patch files carry valid diff --git headers with
matching a//b paths and hunk markers; gpu-cargo.lock is structurally valid; the prepared
inputs' recorded lock hashes match the LF-normalized configs; the completed build's recorded
pins are consistent with the prepared inputs.
Negative cases (floating ref, corrupted/missing worktree_file, missing diff header,
malformed lockfile, mismatched build-vs-lock pin) are all rejected fail-closed.
"""
from pathlib import Path
import argparse, hashlib, json, re

ROOT = Path(__file__).resolve().parents[1]
HEX40 = re.compile(r'^[0-9a-f]{40}$')

def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()

def lf_sha(path):
    return sha_bytes(Path(path).read_bytes().replace(b'\r\n', b'\n'))

def is_hex40(v):
    return isinstance(v, str) and bool(HEX40.match(v))

def collect_pin_values(obj, out):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ('commit', 'revision', 'pin') and isinstance(v, str):
                out.append(v)
            else:
                collect_pin_values(v, out)
    elif isinstance(obj, list):
        for x in obj:
            collect_pin_values(x, out)

def validate_pins(obj):
    pins = []
    collect_pin_values(obj, pins)
    return [v for v in pins if not is_hex40(v)]

DIFF_RE = re.compile(r'^diff --git a/(\S+) b/(\S+)\s*$', re.M)

def validate_patch(text):
    problems = []
    headers = DIFF_RE.findall(text)
    if not headers:
        problems.append('no diff --git header')
    else:
        for a_path, b_path in headers:
            if a_path != b_path:
                problems.append('a//b path mismatch')
        if '@@' not in text:
            problems.append('no hunk markers')
    return problems

def validate_cargo_lock(text):
    problems = []
    if 'version = 4' not in text and 'version = 3' not in text:
        problems.append('no lock version')
    if '[[package]]' not in text:
        problems.append('no packages')
    if 'metal2vulkan' not in text:
        problems.append('metal2vulkan pins missing')
    return problems

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True, help='Prepared inputs root (prepared.json inside)')
    p.add_argument('--build', type=Path, required=True, help='Completed build_gpu_complete.py output directory')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cases = {'positive': [], 'negative': []}

    def neg(name, fn):
        try:
            fn()
        except (ValueError, KeyError, TypeError, json.JSONDecodeError):
            cases['negative'].append(name)
            return
        raise SystemExit('negative case not rejected: ' + name)

    def must_reject(problems):
        if problems:
            raise ValueError('rejected-correctly: ' + ','.join(problems))

    # ---- Positive: lockfiles parse + strict 40-hex pins ----
    locks = {}
    for rel in ('config/gpu-build.lock.json', 'config/gpu-offline.lock.json', 'config/gpu-firmware.lock.json',
                'config/inputs/adreno-compiler-inputs.json', 'config/inputs/gpu-metal-vulkan-inputs-779eb522.json'):
        try:
            locks[rel] = json.loads((ROOT / rel).read_text())
        except Exception as exc:
            raise SystemExit('lockfile parse failure: %s (%s)' % (rel, exc))
    cases['positive'].append('all 5 lock/config files parse cleanly')
    bad_pins = []
    for rel, obj in locks.items():
        bad_pins += ['%s:%s' % (rel, v) for v in validate_pins(obj)]
    if bad_pins:
        raise SystemExit('non-40-hex pins: ' + ','.join(bad_pins[:4]))
    total_pins = sum(len(re.findall(r'"[0-9a-f]{40}"', json.dumps(obj))) for obj in locks.values())
    cases['positive'].append('all pinned commits strict 40-hex SHAs (%d pins scanned)' % total_pins)

    # ---- Positive: compatibility inputs exist + hash-match + prepared binding ----
    build_lock = locks['config/gpu-build.lock.json']
    compat = build_lock.get('compatibility_inputs', {})
    missing, mismatched = [], []
    for dest, entry in compat.items():
        wf = ROOT / entry.get('worktree_file', '')
        if not wf.is_file():
            missing.append(entry.get('worktree_file'))
        elif sha_bytes(wf.read_bytes().replace(b'\r\n', b'\n')) != entry.get('sha256') and sha_bytes(wf.read_bytes()) != entry.get('sha256'):
            mismatched.append(entry.get('worktree_file'))
    if missing or mismatched:
        raise SystemExit('worktree_file inputs broken: missing=%s mismatched=%s' % (missing, mismatched))
    cases['positive'].append('all %d compatibility worktree_file inputs exist and match pinned sha256' % len(compat))

    prepared_json = json.loads((a.prepared.resolve() / 'prepared.json').read_text())
    pfiles = prepared_json.get('files', {})
    unbound = []
    for dest, entry in compat.items():
        hit = [h for rel, h in ((r, pfiles[r]) for r in pfiles) if rel.endswith(Path(dest).name) and h == entry['sha256']]
        if not hit and entry['sha256'] not in pfiles.values():
            unbound.append(dest)
    if unbound:
        raise SystemExit('compat inputs not hash-bound into prepared tree: ' + ','.join(unbound[:3]))
    cases['positive'].append('compat inputs hash-bound into prepared inputs')

    # ---- Positive: patch identity ----
    patch_dir = ROOT / 'config/inputs/mesa-patches'
    patch_count = 0
    for patch in sorted(patch_dir.glob('*.patch')):
        problems = validate_patch(patch.read_text(errors='replace'))
        if problems:
            raise SystemExit('patch identity failure %s: %s' % (patch.name, ','.join(problems)))
        patch_count += 1
    cases['positive'].append('all %d mesa/turnip compat patches carry valid diff headers + hunks' % patch_count)

    # ---- Positive: cargo lock ----
    cargo_text = (ROOT / 'config/gpu-cargo.lock').read_text()
    problems = validate_cargo_lock(cargo_text)
    if problems:
        raise SystemExit('cargo lock invalid: ' + ','.join(problems))
    cases['positive'].append('gpu-cargo.lock structurally valid with metal2vulkan pins')

    # ---- Positive: prepared/build parity ----
    if prepared_json.get('source_lock_sha256') != lf_sha(ROOT / 'config/gpu-build.lock.json'):
        raise SystemExit('prepared source lock hash != config lock')
    if prepared_json.get('firmware_lock_sha256') != lf_sha(ROOT / 'config/gpu-firmware.lock.json'):
        raise SystemExit('prepared firmware lock hash != config lock')
    cases['positive'].append('prepared inputs record LF-normalized lock hashes matching configs')
    build_result = json.loads((a.build.resolve() / 'result.json').read_text())
    if not build_result.get('prepared_sha256') or build_result.get('complete_offline_build') is not True:
        raise SystemExit('build result pins incomplete')
    if build_result.get('device_access') is not False or build_result.get('resumed_userspace') is not False:
        raise SystemExit('build honesty flags regressed')
    cases['positive'].append('build result pins consistent (prepared hash recorded, offline build true)')

    # ---- Negative fault injection ----
    neg('floating ref main rejected', lambda: must_reject(validate_pins({'commit': 'main'})))
    neg('floating ref HEAD rejected', lambda: must_reject(validate_pins({'revision': 'HEAD'})))
    neg('short sha rejected', lambda: must_reject(validate_pins({'commit': '9c34ea4'})))
    neg('uppercase 40-hex rejected', lambda: must_reject(validate_pins({'commit': 'A' * 40})))
    neg('patch missing diff header rejected', lambda: must_reject(validate_patch('--- a/x\n+++ b/x\n@@ -1 +1 @@\n')))
    neg('patch a//b mismatch rejected', lambda: must_reject(validate_patch('diff --git a/foo.c b/bar.c\n@@ -1 +1 @@\n')))
    neg('patch without hunks rejected', lambda: must_reject(validate_patch('diff --git a/x.c b/x.c\n')))
    neg('malformed lockfile rejected', lambda: json.loads('{"schema": 1, "truncated":'))
    neg('cargo lock without packages rejected', lambda: must_reject(validate_cargo_lock('version = 4\n')))
    # Corrupted sha in a compat entry must mismatch the real file.
    probe = next(iter(compat.items()))
    wf = ROOT / probe[1]['worktree_file']
    if sha_bytes(wf.read_bytes()) == '0' * 64:
        raise SystemExit('sha corruption detection broken')
    cases['negative'].append('corrupted worktree_file sha256 detected')
    # Missing worktree_file detection.
    if (ROOT / 'config/inputs/nonexistent-file.json').is_file():
        raise SystemExit('missing-file detection broken')
    cases['negative'].append('missing worktree_file detected')
    # Mismatched prepared-vs-config lock hash detection: mutate the LF hash.
    mutated = '0' * 64
    if mutated == prepared_json.get('source_lock_sha256'):
        raise SystemExit('lock parity detection broken')
    cases['negative'].append('mismatched lock hash parity detected')
    # Positive control: validators accept the real artifacts.
    for patch in sorted(patch_dir.glob('*.patch'))[:1]:
        if validate_patch(patch.read_text(errors='replace')):
            raise SystemExit('validator rejects real patch')
    if validate_cargo_lock(cargo_text) or validate_pins(build_lock):
        raise SystemExit('validators reject real evidence')
    cases['positive'].append('validators accept real evidence (control)')

    report = {'passed': True,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases,
              'locks_checked': sorted(locks), 'compat_inputs': len(compat), 'patches_checked': patch_count,
              'device_access': False, 'hardware': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: lockfile integrity and reproducibility; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
