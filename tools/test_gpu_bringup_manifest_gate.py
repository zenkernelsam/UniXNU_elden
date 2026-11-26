"""Gate wrapper for the bringup-manifest phase (phase 16 of the unified gate).

Runs the deterministic generator's self-test, then generates bringup-manifest.json
twice against this round's complete/package/verify evidence and validates:
deterministic repeated emission (byte-identical prewarm key), the 11-item activation
contract exactly, gate_rollup consistency with the preceding phases and 17 guardrails,
and the honesty flags. Emits its own result.json for the unified gate rollup.
"""
from pathlib import Path
import argparse, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'tools' / 'generate_gpu_bringup_manifest.py'

EXPECTED_CONTRACT = {
    'boot_arg': 'elden-gpu=1',
    'default_enabled': False,
    'tu_debug': 'sysmem,nocb',
    'sp_cores': 6,
    'execution_regions_preserved': 30,
    'abi_instance_bytes': 136,
    'abi_vtable_entries': 168,
    'hardware_qualified': False,
    'device_access': False,
    'boot_image_packaged': False,
    'live_policy_selected': False,
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--verify', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    positive, negative, failures = [], [], []

    def check(name, ok, detail=''):
        if ok:
            positive.append(name)
        else:
            failures.append(name + (': ' + detail if detail else ''))

    # 1. generator self-test (2 positive + 5 negative inside)
    r = subprocess.run([sys.executable, str(GENERATOR), '--self-test'], capture_output=True, text=True)
    check('generator self-test passes', r.returncode == 0, (r.stdout + r.stderr)[-160:])

    # 2. generate twice; deterministic prewarm key and byte-identical emission
    m1 = out / 'manifest-first.json'
    m2 = out / 'manifest-second.json'
    cmd = [sys.executable, str(GENERATOR), '--build', str(a.build.resolve()),
           '--package', str(a.package.resolve()), '--verify', str(a.verify.resolve())]
    r1 = subprocess.run(cmd + ['--output', str(m1)], capture_output=True, text=True)
    check('first manifest generation succeeds', r1.returncode == 0, (r1.stdout + r1.stderr)[-160:])
    r2 = subprocess.run(cmd + ['--output', str(m2)], capture_output=True, text=True)
    check('second manifest generation succeeds', r2.returncode == 0, (r2.stdout + r2.stderr)[-160:])
    if r1.returncode == 0 and r2.returncode == 0:
        d1 = json.loads(m1.read_text())
        d2 = json.loads(m2.read_text())
        key = d1.get('prewarm_key', {}).get('sha256') or ''
        check('prewarm key 64-hex present', len(key) == 64 and all(c in '0123456789abcdef' for c in key), key[:12])
        check('prewarm key deterministic across repeated generation', key == d2.get('prewarm_key', {}).get('sha256'))
        check('manifest byte-deterministic', m1.read_bytes() == m2.read_bytes())
        contract = d1.get('activation_contract', {})
        mismatched = sorted(k for k, v in EXPECTED_CONTRACT.items() if contract.get(k) != v)
        check('activation contract 11 items exact', not mismatched, ','.join(mismatched))
        rollup = d1.get('gate_rollup', {})
        check('gate rollup 17 guardrails true', rollup.get('guardrails_true') == 17
              and rollup.get('guardrails_total') == 17,
              '%s/%s' % (rollup.get('guardrails_true'), rollup.get('guardrails_total')))
        check('gate rollup phases all passed', rollup.get('phases_passed') == rollup.get('phases_total')
              and (rollup.get('phases_total') or 0) >= 15,
              '%s/%s' % (rollup.get('phases_passed'), rollup.get('phases_total')))
        check('honesty flags false in manifest',
              d1.get('device_access') is False and d1.get('hardware_qualified') is False)
        if r1.returncode == 0:
            final = a.verify.resolve() / 'bringup-manifest.json'
            final.write_bytes(m1.read_bytes())

    # 3. negative coverage: the generator's own fail-closed battery (missing LC_UUID,
    # failed phase, hardware_qualified true, false guardrail, resumed userspace) is
    # exercised inside its --self-test and counted here when that self-test passes.
    if r.returncode == 0:
        negative.append('generator negative battery (5 cases) enforced inside self-test')

    passed = not failures
    report = {
        'passed': passed,
        'case_counts': {'positive': len(positive), 'negative': len(negative)},
        'positive': positive, 'negative': negative, 'failures': failures,
        'prewarm_key': (json.loads(m1.read_text())['prewarm_key']['sha256']
                        if r1.returncode == 0 and m1.is_file() else None),
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('bringup-manifest gate failures: ' + '; '.join(failures))
    print('PASS: bringup-manifest gate; %d positive + %d negative' % (len(positive), len(negative)))


if __name__ == '__main__':
    main()
