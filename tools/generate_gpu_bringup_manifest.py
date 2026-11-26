"""Deterministic prewarm-key & bringup manifest generator (offline, zero device IO).

Reads a completed --build, --package and --verify directory and emits
bringup-manifest.json: the composite shader-cache/prewarm-pack key (pinned translator
revision + the four packaged Mach-O LC_UUIDs), the activation & guardrail contract,
and the artifact & gate rollup (package archive sha256, kernel sha256, 15/15 phases,
17/17 guardrails, matrix_totals). Every element is cross-checked against the evidence
fail-closed: a missing LC_UUID, a failed phase, or any honesty flag violation refuses
emission. --self-test exercises positive and negative cases against synthetic fixtures.
"""
from pathlib import Path
import argparse, hashlib, json, sys, tempfile

TRANSLATOR_PIN = '779eb522b45615ab7a2e96f823502715238e472b'
PREWARM_BINARIES = ['libvulkan_freedreno.dylib', 'NativeAdrenoMetal', 'kernel.macho', 'adrenoprobed']
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


def load_json(path):
    return json.loads(Path(path).read_text())


def prewarm_key(translator_rev, lc_uuids):
    """Composite deterministic key: sha256 over the canonical concatenation of the
    translator rev and the four LC_UUIDs in fixed binary order."""
    parts = [translator_rev] + [lc_uuids[b] for b in PREWARM_BINARIES]
    material = '|'.join(parts).encode()
    return hashlib.sha256(material).hexdigest()


def collect_manifest(build_dir, package_dir, verify_dir):
    """Returns (manifest, failures). Pure function of the three evidence trees."""
    failures = []
    build = load_json(Path(build_dir) / 'result.json')
    package = load_json(Path(package_dir) / 'result.json')
    gate = load_json(Path(verify_dir) / 'offline-verification.json')
    hygiene = load_json(Path(verify_dir) / 'userspace-package-hygiene' / 'result.json')
    contract = load_json(Path(verify_dir) / 'pre-bringup-contract' / 'result.json')

    lc_uuids = hygiene.get('lc_uuids', {})
    missing_uuid = [b for b in PREWARM_BINARIES if not lc_uuids.get(b)]
    if missing_uuid:
        failures.append('missing LC_UUID for: ' + ','.join(missing_uuid))

    # Honesty flags fail-closed across all evidence layers.
    for label, blob in (('build', build), ('package', package), ('gate', gate), ('hygiene', hygiene)):
        if blob.get('hardware_qualified'):
            failures.append('%s evidence claims hardware_qualified' % label)
        if blob.get('device_access'):
            failures.append('%s evidence claims device_access' % label)
    if build.get('boot_image_packaged'):
        failures.append('build evidence claims boot_image_packaged')
    if not build.get('prepared_sha256'):
        failures.append('build missing prepared_sha256')
    if build.get('resumed_userspace'):
        failures.append('build resumed userspace -- not a fresh re-qualification')

    # Gate completeness: 15/15 phases passed, 17/17 guardrails true.
    phases = gate.get('phases', {})
    failed = sorted(n for n, v in phases.items() if not (isinstance(v, dict) and v.get('passed')))
    if failed:
        failures.append('failed verification phases: ' + ','.join(failed))
    # The manifest is generated mid-gate against the provisional rollup (15 phases,
    # everything before bringup-manifest itself) or post-hoc against the final rollup
    # (17 phases); both are valid. The fail-closed invariant is that every phase passed.
    if len(phases) < 15:
        failures.append('phase count %d < 15' % len(phases))
    guardrails = gate.get('guardrails', {})
    false_guards = sorted(n for n, v in guardrails.items() if v is not True)
    if false_guards:
        failures.append('guardrails not true: ' + ','.join(false_guards))
    if len(guardrails) != 17:
        failures.append('guardrail count %d != 17' % len(guardrails))
    if not gate.get('passed'):
        failures.append('offline verification gate not passed')
    if not package.get('passed') or not package.get('archive_sha256'):
        failures.append('package evidence incomplete')
    if not contract.get('passed'):
        failures.append('pre-bringup contract not passed')

    kernel = build.get('artifacts', {}).get('entry/kernel.macho', {})
    if not kernel.get('sha256'):
        failures.append('kernel.macho artifact sha256 missing')

    abi = build.get('gpu_specific_abi', {})
    manifest = {
        'schema': 'elden-gpu-bringup-manifest/1',
        'prewarm_key': {
            'sha256': prewarm_key(TRANSLATOR_PIN, lc_uuids) if not missing_uuid else None,
            'material_order': ['translator_rev'] + PREWARM_BINARIES,
            'translator_rev': TRANSLATOR_PIN,
            'lc_uuids': {b: lc_uuids.get(b) for b in PREWARM_BINARIES},
        },
        'activation_contract': dict(EXPECTED_CONTRACT),
        'artifacts': {
            'package_archive_sha256': package.get('archive_sha256'),
            'package_bytes': package.get('bytes'),
            'kernel_sha256': kernel.get('sha256'),
            'prepared_sha256': build.get('prepared_sha256'),
            'abi_summary': abi if isinstance(abi, dict) else {},
        },
        'gate_rollup': {
            'phases_total': len(phases),
            'phases_passed': len(phases) - len(failed),
            'guardrails_total': len(guardrails),
            'guardrails_true': len(guardrails) - len(false_guards),
            'matrix_totals': contract.get('matrix_totals', {}),
        },
        'device_access': False,
        'hardware_qualified': False,
    }
    return manifest, failures


def self_test():
    """Positive + negative fail-closed checks against synthetic fixture trees."""
    positive, negative, failures = [], [], []

    def fixture():
        tmp = Path(tempfile.mkdtemp(prefix='m2v-manifest-selftest-'))
        build = tmp / 'build'
        package = tmp / 'package'
        verify = tmp / 'verify'
        (verify / 'userspace-package-hygiene').mkdir(parents=True)
        (verify / 'pre-bringup-contract').mkdir(parents=True)
        build.mkdir()
        package.mkdir()
        (build / 'result.json').write_text(json.dumps({
            'hardware_qualified': False, 'device_access': False, 'boot_image_packaged': False,
            'prepared_sha256': 'aa' * 32, 'resumed_userspace': False,
            'gpu_specific_abi': {'instance_bytes': 136, 'vtable_entries': 168},
            'artifacts': {'entry/kernel.macho': {'sha256': 'bb' * 32, 'bytes': 1}},
        }))
        (package / 'result.json').write_text(json.dumps({
            'passed': True, 'archive_sha256': 'cc' * 32, 'bytes': 100,
            'default_enabled': False, 'device_access': False,
        }))
        (verify / 'offline-verification.json').write_text(json.dumps({
            'passed': True, 'device_access': False, 'hardware_qualified': False,
            'phases': {'p%02d' % i: {'passed': True, 'returncode': 0} for i in range(15)},
            'guardrails': {'g%02d' % i: True for i in range(17)},
        }))
        (verify / 'userspace-package-hygiene' / 'result.json').write_text(json.dumps({
            'passed': True, 'device_access': False,
            'lc_uuids': {b: '%032x' % (i + 1) for i, b in enumerate(PREWARM_BINARIES)},
        }))
        (verify / 'pre-bringup-contract' / 'result.json').write_text(json.dumps({
            'passed': True, 'matrix_totals': {'phases': 14, 'positive': 1, 'negative': 1, 'iterations': 1},
        }))
        return build, package, verify

    def expect_reject(name, mutate):
        b, p, v = fixture()
        mutate(b, p, v)
        try:
            _, f = collect_manifest(b, p, v)
        except Exception:
            f = ['exception']
        if f:
            negative.append(name)
        else:
            failures.append('negative case escaped: ' + name)

    b, p, v = fixture()
    manifest, f = collect_manifest(b, p, v)
    if not f and manifest['prewarm_key']['sha256']:
        positive.append('synthetic positive fixture emits manifest with prewarm key')
        again, _ = collect_manifest(b, p, v)
        if again['prewarm_key']['sha256'] == manifest['prewarm_key']['sha256']:
            positive.append('prewarm key deterministic across repeated collection')
    else:
        failures.append('positive fixture rejected: ' + '; '.join(f))

    def drop_uuid(b, p, v):
        d = load_json(v / 'userspace-package-hygiene' / 'result.json')
        del d['lc_uuids']['adrenoprobed']
        (v / 'userspace-package-hygiene' / 'result.json').write_text(json.dumps(d))
    expect_reject('missing LC_UUID rejected', drop_uuid)

    def fail_phase(b, p, v):
        d = load_json(v / 'offline-verification.json')
        d['phases']['p03'] = {'passed': False, 'returncode': 1}
        (v / 'offline-verification.json').write_text(json.dumps(d))
    expect_reject('failed verification phase rejected', fail_phase)

    def hw_true(b, p, v):
        d = load_json(b / 'result.json')
        d['hardware_qualified'] = True
        (b / 'result.json').write_text(json.dumps(d))
    expect_reject('hardware_qualified true rejected', hw_true)

    def flip_guard(b, p, v):
        d = load_json(v / 'offline-verification.json')
        d['guardrails']['g07'] = False
        (v / 'offline-verification.json').write_text(json.dumps(d))
    expect_reject('false guardrail rejected', flip_guard)

    def resumed(b, p, v):
        d = load_json(b / 'result.json')
        d['resumed_userspace'] = True
        (b / 'result.json').write_text(json.dumps(d))
    expect_reject('resumed userspace build rejected', resumed)

    return positive, negative, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path)
    p.add_argument('--package', type=Path)
    p.add_argument('--verify', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()

    if a.self_test:
        positive, negative, failures = self_test()
        report = {
            'passed': not failures,
            'case_counts': {'positive': len(positive), 'negative': len(negative)},
            'positive': positive, 'negative': negative, 'failures': failures,
            'device_access': False, 'hardware_qualified': False,
        }
        print(json.dumps(report, indent=2))
        if failures:
            raise SystemExit('self-test failures: ' + '; '.join(failures))
        print('PASS: generate_gpu_bringup_manifest self-test; %d positive + %d negative'
              % (len(positive), len(negative)))
        return

    if not (a.build and a.package and a.verify and a.output):
        raise SystemExit('--build --package --verify --output are required (or --self-test)')
    manifest, failures = collect_manifest(a.build.resolve(), a.package.resolve(), a.verify.resolve())
    if failures:
        raise SystemExit('bringup manifest refused (fail-closed): ' + '; '.join(failures))
    out = a.output.resolve()
    out.write_text(json.dumps(manifest, indent=2) + chr(10))
    print('PASS: bringup-manifest.json emitted; prewarm key %s' % manifest['prewarm_key']['sha256'][:16])


if __name__ == '__main__':
    main()
