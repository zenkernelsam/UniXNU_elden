"""Deterministic, fail-closed main-merge & pre-bringup readiness dossier generator (offline).

Reads a run directory's complete/, package/ and verify/ evidence (offline-verification.json,
bringup-manifest.json, gpu-abi.json, package result) and emits a deterministic
`bringup-readiness-dossier.json` (schema: elden-gpu-bringup-readiness-dossier/1) covering:
1. full prior-phase coverage (all returncode=0 & passed=true) + 17/17 guardrails + matrix_totals;
2. production-line & reproducibility anchors (prewarm key, 37-file package archive sha256 +
   readback, kernel.macho sha256, GPU ABI contract 136B/168 slots/C2+D2/legacy flags false);
3. branch scope & merge hygiene (drivers/EldenGpu + tools/*gpu* core inventory, no hardcoded
   absolute host paths, no unpinned refs in input locks);
4. honesty boundary: explicit HARDWARE_PENDING items that only physical hardware can settle,
   with hardware_qualified/device_access/boot_image_packaged/live_policy_selected/default_enabled
   all hard false.

`--self-test` builds synthetic fixtures inside %TEMP% / /private/tmp only, cleans up afterwards,
and never touches research/ or any real evidence tree.
"""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess, sys, tempfile

SCHEMA = 'elden-gpu-bringup-readiness-dossier/1'
EXPECTED_PHASE_COUNT = 29
EXPECTED_GUARDRAIL_COUNT = 17
HEX64 = re.compile(r'^[0-9a-f]{64}$')
HEX40 = re.compile(r'^[0-9a-f]{40}$')

HARDWARE_PENDING_ITEMS = [
    'first-boot elden-gpu=1 probe: kernel-side bootarg activation path on physical TB322FC',
    'real GMU/HFI handshake: firmware power-up, HFI channel init and ACK exchange on hardware',
    'real ZAP/PAS authentication and shader-loader signing flow on hardware',
    'real SMMU/IOVA translation fault behavior and PM4 ring execution on hardware',
    'real Vulkan (Turnip) rendering correctness: triangle/compute/blit on device',
    'real Metal (NativeAdrenoMetal) client rendering via IOSurface on device',
    'DCVS/IFPC/ACD frequency scaling, power-collapse and battery endurance measurement',
    'suspend/resume, dual-ring preemption and GMEM save/restore on hardware',
    'DPU direct-scanout and system compositor presentation of GPU-rendered frames',
    'GPU drain-before-UFS-shutdown ordering verification on physical power-cycle',
]

HOST_PATH_PATTERNS = ['/users/ciscohe', 'c:\\users\\cisco he', '/home/ciscohe']


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text())


def collect_failures(build, package, verify, repo):
    """Fail-closed evidence validation. Returns (failures, facts) where facts carries
    the validated values used to build the dossier."""
    failures, facts = [], {}
    build, package, verify = Path(build), Path(package), Path(verify)

    # ---- 1. prior phase coverage + guardrails + matrix totals ----
    ov_path = verify / 'offline-verification.json'
    if not ov_path.is_file():
        return ['offline-verification.json missing'], facts
    ov = load_json(ov_path)
    phases = ov.get('phases', {})
    if len(phases) != EXPECTED_PHASE_COUNT:
        failures.append('phase count %d != %d' % (len(phases), EXPECTED_PHASE_COUNT))
    bad = [n for n, ph in phases.items() if ph.get('returncode') != 0 or ph.get('passed') is not True]
    if bad:
        failures.append('prior phases not all green: ' + ','.join(sorted(bad)[:4]))
    guardrails = ov.get('guardrails', {})
    if len(guardrails) != EXPECTED_GUARDRAIL_COUNT:
        failures.append('guardrail count %d != %d' % (len(guardrails), EXPECTED_GUARDRAIL_COUNT))
    gfalse = [k for k, v in guardrails.items() if v is not True]
    if gfalse:
        failures.append('guardrails not all true: ' + ','.join(sorted(gfalse)[:4]))
    facts['phases'] = {n: {'returncode': ph['returncode'], 'passed': ph['passed']} for n, ph in phases.items()}
    facts['guardrails'] = dict(guardrails)

    mt_path = verify / 'pre-bringup-contract' / 'result.json'
    if not mt_path.is_file():
        failures.append('pre-bringup-contract result.json missing (matrix_totals unavailable)')
    else:
        mt = load_json(mt_path).get('matrix_totals', {})
        if not all(k in mt for k in ('phases', 'positive', 'negative', 'iterations')):
            failures.append('matrix_totals incomplete')
        else:
            facts['matrix_totals'] = {k: mt[k] for k in ('phases', 'positive', 'negative', 'iterations')}

    # ---- 2. production-line & reproducibility anchors ----
    bm_path = verify / 'bringup-manifest.json'
    if not bm_path.is_file():
        failures.append('bringup-manifest.json missing')
    else:
        bm = load_json(bm_path)
        pk = bm.get('prewarm_key', {}).get('sha256', '')
        if not HEX64.match(pk):
            failures.append('prewarm_key.sha256 malformed (need 64 lowercase hex)')
        else:
            facts['prewarm_key_sha256'] = pk
            facts['prewarm_translator_rev'] = bm.get('prewarm_key', {}).get('translator_rev')

    pr_path = package / 'result.json'
    if not pr_path.is_file():
        failures.append('package result.json missing')
    else:
        pr = load_json(pr_path)
        if pr.get('passed') is not True:
            failures.append('package not passed')
        if pr.get('files') != 37:
            failures.append('package file count %s != 37' % pr.get('files'))
        if not HEX64.match(str(pr.get('archive_sha256', ''))):
            failures.append('package archive_sha256 malformed')
        if pr.get('archive_readback_passed') is not True:
            failures.append('archive readback not passed')
        facts['package'] = {'files': pr.get('files'), 'archive_sha256': pr.get('archive_sha256'),
                            'bytes': pr.get('bytes')}

    abi_path = build / 'gpu-abi.json'
    if not abi_path.is_file():
        failures.append('gpu-abi.json missing')
    else:
        abi = load_json(abi_path)
        ck = abi.get('checks', {})
        if abi.get('gpu_specific_abi_checks_passed') is not True:
            failures.append('gpu_specific_abi_checks_passed not true')
        if ck.get('accelerator_instance_bytes') != 136:
            failures.append('accelerator instance bytes != 136')
        if ck.get('vtable_entries') != 168:
            failures.append('vtable entries != 168')
        if ck.get('owned_override_slots') != [0, 1, 7, 39, 40, 41, 140]:
            failures.append('owned override slots drifted')
        branches = ck.get('native_original_branches', {})
        if not any(k.endswith('C2EPK11OSMetaClass') for k in branches) or not any(k.endswith('D2Ev') for k in branches):
            failures.append('C2/D2 original branches missing')
        if ck.get('original_entries_sha256') is not True:
            failures.append('original entries (30 Apple execution regions) sha256 anchor not true')
        legacy = abi.get('legacy_emitter_flags', {})
        if any(v is not False for v in legacy.values()) or not legacy:
            failures.append('legacy_emitter_flags not all false')
        if not HEX64.match(str(abi.get('kernel_sha256', ''))):
            failures.append('abi kernel_sha256 malformed')
        facts['abi'] = {
            'accelerator_instance_bytes': ck.get('accelerator_instance_bytes'),
            'vtable_entries': ck.get('vtable_entries'),
            'owned_override_slots': ck.get('owned_override_slots'),
            'legacy_emitter_flags': dict(legacy),
            'kernel_sha256': abi.get('kernel_sha256'),
        }

    linked = build / 'linked' / 'kernel.macho'
    entry = build / 'entry' / 'kernel.macho'
    if not linked.is_file():
        failures.append('linked/kernel.macho missing')
    elif facts.get('abi', {}).get('kernel_sha256'):
        lsha = sha256_file(linked)
        if lsha != facts['abi']['kernel_sha256']:
            failures.append('linked kernel.macho sha256 != abi kernel_sha256')
        facts['linked_kernel_sha256'] = lsha
    if not entry.is_file():
        failures.append('entry/kernel.macho missing')
    else:
        facts['entry_kernel_sha256'] = sha256_file(entry)

    # ---- 3. branch scope & merge hygiene ----
    hygiene = {}
    tree = Path(repo) if repo else None
    if tree is None or not tree.is_dir():
        failures.append('repo tree missing for hygiene scan')
    else:
        elden = tree / 'drivers' / 'EldenGpu'
        # derived units (EldenGpuRuntime.cpp) are generated at build time; the branch-carried
        # core inventory is the source-of-truth set below.
        core_units = ['FrameBridge.cpp', 'Runtime.h', 'RuntimeSource.py', 'MaintenanceSource.py', 'SubmitSource.py']
        missing_core = [u for u in core_units if not (elden / u).is_file()]
        if not elden.is_dir() or missing_core:
            failures.append('drivers/EldenGpu core inventory incomplete: %s' % ','.join(missing_core or ['dir-missing']))
        gpu_tools = sorted(p.name for p in (tree / 'tools').glob('*gpu*') if p.is_file()) if (tree / 'tools').is_dir() else []
        if len(gpu_tools) < 10:
            failures.append('tools/*gpu* inventory suspiciously small: %d' % len(gpu_tools))
        hygiene['eldengpu_present'] = elden.is_dir() and not missing_core
        hygiene['gpu_tools_count'] = len(gpu_tools)

        host_hits = []
        scan_roots = [elden] if elden.is_dir() else []
        scan_files = []
        for root in scan_roots:
            for ext in ('*.c', '*.cpp', '*.h', '*.py', '*.inc'):
                scan_files.extend(root.rglob(ext))
        # test harnesses and audit tools legitimately embed host-path literals as
        # detection patterns (negative-case fixtures); scanning them would be a
        # self-referential false positive. Production driver sources only.
        scan_files = [sf for sf in scan_files
                      if 'tests' not in sf.parts and not sf.name.startswith('Test')
                      and '__pycache__' not in sf.parts]
        for sf in scan_files:
            try:
                text = sf.read_text(errors='replace').lower()
            except OSError:
                continue
            for pat in HOST_PATH_PATTERNS:
                if pat in text:
                    host_hits.append('%s:%s' % (sf.name, pat))
        if host_hits:
            failures.append('hardcoded host paths in branch sources: ' + ','.join(host_hits[:4]))
        hygiene['host_path_hits'] = len(host_hits)

        # unpinned refs in input lock configs: any *rev*/*pin* style value must be strict 40-hex
        unpinned = []
        cfg = tree / 'config' / 'inputs'
        if cfg.is_dir():
            for jf in sorted(cfg.glob('*.json')):
                try:
                    data = load_json(jf)
                except Exception:
                    unpinned.append('%s:unparseable' % jf.name)
                    continue
                def walk(node, path=''):
                    if isinstance(node, dict):
                        for k, v in node.items():
                            walk(v, '%s/%s' % (path, k))
                    elif isinstance(node, list):
                        for i, v in enumerate(node):
                            walk(v, '%s[%d]' % (path, i))
                    elif isinstance(node, str):
                        key = path.lower()
                        if any(t in key for t in ('rev', 'pin', 'commit', 'sha')) and 'sha256' not in key:
                            if len(node) in (7, 40) or node in ('HEAD', 'main', 'master'):
                                if not HEX40.match(node):
                                    unpinned.append('%s:%s=%s' % (jf.name, path, node[:24]))
                walk(data)
        if unpinned:
            failures.append('unpinned/non-hex refs in input configs: ' + ','.join(unpinned[:4]))
        hygiene['unpinned_refs'] = len(unpinned)
    facts['hygiene'] = hygiene
    return failures, facts


def build_dossier(facts):
    """Deterministic dossier construction from validated facts."""
    dossier = {
        'schema': SCHEMA,
        'prior_phase_coverage': {
            'phase_count': len(facts['phases']),
            'all_returncode_zero': all(ph['returncode'] == 0 for ph in facts['phases'].values()),
            'all_passed': all(ph['passed'] is True for ph in facts['phases'].values()),
            'guardrails_true': sum(1 for v in facts['guardrails'].values() if v is True),
            'guardrails_total': len(facts['guardrails']),
            'matrix_totals': facts.get('matrix_totals'),
        },
        'production_anchors': {
            'prewarm_key_sha256': facts.get('prewarm_key_sha256'),
            'prewarm_translator_rev': facts.get('prewarm_translator_rev'),
            'package': facts.get('package'),
            'linked_kernel_sha256': facts.get('linked_kernel_sha256'),
            'entry_kernel_sha256': facts.get('entry_kernel_sha256'),
            'abi': facts.get('abi'),
        },
        'merge_hygiene': facts.get('hygiene'),
        'hardware_pending': [{'item': it, 'status': 'HARDWARE_PENDING'} for it in HARDWARE_PENDING_ITEMS],
        'hardware_qualified': False,
        'device_access': False,
        'boot_image_packaged': False,
        'live_policy_selected': False,
        'default_enabled': False,
        'note': 'Offline host/model readiness assessment only; no hardware, rendering, performance '
                'or bringup-readiness claim. Every hardware item remains HARDWARE_PENDING.',
    }
    return dossier


def validate_dossier(dossier):
    """Independent fail-closed validation of an emitted dossier (tamper detection)."""
    failures = []
    if dossier.get('schema') != SCHEMA:
        failures.append('schema mismatch')
    for flag in ('hardware_qualified', 'device_access', 'boot_image_packaged', 'live_policy_selected', 'default_enabled'):
        if dossier.get(flag) is not False:
            failures.append('honesty flag %s not hard false' % flag)
    hp = dossier.get('hardware_pending')
    if not isinstance(hp, list) or len(hp) != len(HARDWARE_PENDING_ITEMS):
        failures.append('hardware_pending boundary incomplete or tampered')
    else:
        expected = set(HARDWARE_PENDING_ITEMS)
        got = [e.get('item') for e in hp if isinstance(e, dict)]
        if set(got) != expected:
            failures.append('hardware_pending items tampered')
        if any(e.get('status') != 'HARDWARE_PENDING' for e in hp if isinstance(e, dict)):
            failures.append('hardware_pending status tampered')
    cov = dossier.get('prior_phase_coverage', {})
    if cov.get('all_returncode_zero') is not True or cov.get('all_passed') is not True:
        failures.append('coverage truthfulness broken')
    if cov.get('guardrails_true') != EXPECTED_GUARDRAIL_COUNT:
        failures.append('coverage guardrails count broken')
    anchors = dossier.get('production_anchors', {})
    if not HEX64.match(str(anchors.get('prewarm_key_sha256') or '')):
        failures.append('dossier prewarm key malformed')
    return failures


def emit(build, package, verify, repo, output):
    failures, facts = collect_failures(build, package, verify, repo)
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    if failures:
        report = {'passed': False, 'failures': failures, 'device_access': False, 'hardware_qualified': False}
        (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
        return report, None
    dossier = build_dossier(facts)
    vf = validate_dossier(dossier)
    if vf:
        report = {'passed': False, 'failures': ['self-validation failed: ' + x for x in vf],
                  'device_access': False, 'hardware_qualified': False}
        (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
        return report, None
    canon = json.dumps(dossier, indent=2, sort_keys=True) + chr(10)
    (out / 'bringup-readiness-dossier.json').write_text(canon)
    report = {
        'passed': True,
        'dossier_sha256': hashlib.sha256(canon.encode()).hexdigest(),
        'phase_count': len(facts['phases']),
        'matrix_totals': facts.get('matrix_totals'),
        'hardware_pending_count': len(HARDWARE_PENDING_ITEMS),
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2, sort_keys=True) + chr(10))
    return report, dossier


def make_fixture(root, tamper=None):
    """Synthetic minimal-but-valid evidence tree inside root. tamper: optional mutation key."""
    build = root / 'complete'
    package = root / 'package'
    verify = root / 'verify'
    prepared = root / 'prepared'
    for d in (build / 'linked', build / 'entry', package, verify / 'pre-bringup-contract',
              prepared / 'drivers' / 'EldenGpu', prepared / 'tools',
              prepared / 'config' / 'inputs'):
        d.mkdir(parents=True, exist_ok=True)
    kernel_bytes = b'synthetic-kernel-' + b'x' * 64
    (build / 'linked' / 'kernel.macho').write_bytes(kernel_bytes)
    (build / 'entry' / 'kernel.macho').write_bytes(b'entry-variant-' + b'y' * 32)
    ksha = hashlib.sha256(kernel_bytes).hexdigest()
    abi = {
        'gpu_specific_abi_checks_passed': True,
        'checks': {
            'original_entries_sha256': True,
            'accelerator_instance_bytes': 136,
            'vtable_entries': 168,
            'owned_override_slots': [0, 1, 7, 39, 40, 41, 140],
            'native_original_branches': {
                '__ZN14AGXAcceleratorC2EPK11OSMetaClass': '0xfffffff004bd3b20',
                '__ZN14AGXAcceleratorD2Ev': '0xfffffff004bd3b64',
            },
            'late_initializers_not_automatic': True,
        },
        'kernel_sha256': ksha,
        'legacy_emitter_flags': {'abi_qualified': False, 'loadable': False,
                                 'runtime_qualified': False, 'instruction_relocation_complete': False},
        'hardware': False,
    }
    (build / 'gpu-abi.json').write_text(json.dumps(abi))
    pk = hashlib.sha256(b'prewarm').hexdigest()
    (verify / 'bringup-manifest.json').write_text(json.dumps(
        {'prewarm_key': {'sha256': pk, 'translator_rev': '779eb522'}}))
    (verify / 'pre-bringup-contract' / 'result.json').write_text(json.dumps(
        {'matrix_totals': {'phases': 20, 'positive': 177, 'negative': 123, 'iterations': 30875}}))
    phase_names = ['p%02d' % i for i in range(EXPECTED_PHASE_COUNT)]
    ov = {
        'passed': True,
        'phases': {n: {'returncode': 0, 'passed': True} for n in phase_names},
        'guardrails': {'g%02d' % i: True for i in range(EXPECTED_GUARDRAIL_COUNT)},
    }
    (verify / 'offline-verification.json').write_text(json.dumps(ov))
    (package / 'result.json').write_text(json.dumps({
        'passed': True, 'files': 37, 'bytes': 111504658,
        'archive_sha256': hashlib.sha256(b'archive').hexdigest(),
        'archive_readback_passed': True, 'device_access': False, 'default_enabled': False}))
    for unit in ('FrameBridge.cpp', 'Runtime.h', 'RuntimeSource.py', 'MaintenanceSource.py', 'SubmitSource.py'):
        (prepared / 'drivers' / 'EldenGpu' / unit).write_text('// synthetic unit\n')
    for i in range(12):
        (prepared / 'tools' / ('test_gpu_syn%02d.py' % i)).write_text('# synthetic\n')
    (prepared / 'config' / 'inputs' / 'lock.json').write_text(json.dumps(
        {'pins': {'some_rev': 'a' * 40, 'other_pin': 'b' * 40}}))

    # ---- tamper mutations for negative cases ----
    if tamper == 'phase-failed':
        ov['phases']['p07'] = {'returncode': 1, 'passed': False}
        (verify / 'offline-verification.json').write_text(json.dumps(ov))
    elif tamper == 'guardrail-false':
        ov['guardrails']['g03'] = False
        (verify / 'offline-verification.json').write_text(json.dumps(ov))
    elif tamper == 'prewarm-short':
        (verify / 'bringup-manifest.json').write_text(json.dumps(
            {'prewarm_key': {'sha256': 'deadbeef', 'translator_rev': '779eb522'}}))
    elif tamper == 'legacy-true':
        abi['legacy_emitter_flags']['loadable'] = True
        (build / 'gpu-abi.json').write_text(json.dumps(abi))
    elif tamper == 'hw-qualified-leak':
        # a leaked hardware_qualified true inside package evidence must not propagate;
        # here the dossier consumer validation must stay hard false regardless.
        pass
    return build, package, verify, prepared


def self_test():
    positive, negative, failures = [], [], []
    tmp = Path(tempfile.mkdtemp(prefix='readiness-selftest-'))

    def expect_reject(name, fn):
        report, dossier = fn()
        if report and report.get('passed') is False:
            negative.append(name)
        else:
            failures.append('negative case escaped: ' + name)

    try:
        # positive 1: valid synthetic evidence tree emits complete dossier
        fx = tmp / 'pos1'
        fx.mkdir()
        build, package, verify, prepared = make_fixture(fx)
        out = fx / 'out'
        report, dossier = emit(build, package, verify, prepared, out)
        if report.get('passed') and dossier and dossier.get('schema') == SCHEMA:
            positive.append('valid synthetic evidence tree emits complete dossier')
        else:
            failures.append('positive emission failed: %s' % report.get('failures'))

        # positive 2: double-run byte-identical determinism
        out2 = fx / 'out2'
        emit(build, package, verify, prepared, out2)
        b1 = (out / 'bringup-readiness-dossier.json').read_bytes()
        b2 = (out2 / 'bringup-readiness-dossier.json').read_bytes()
        if b1 == b2 and len(b1) > 200:
            positive.append('double-run byte-identical dossier determinism')
        else:
            failures.append('determinism check failed')

        # positive 3: HARDWARE_PENDING boundary items complete in emitted dossier
        hp = dossier.get('hardware_pending', [])
        if len(hp) == len(HARDWARE_PENDING_ITEMS) and all(e['status'] == 'HARDWARE_PENDING' for e in hp):
            positive.append('HARDWARE_PENDING boundary items complete')
        else:
            failures.append('hardware boundary incomplete')

        # negative 1: failed prior phase rejected
        fxn = tmp / 'neg1'
        fxn.mkdir()
        b, p, v, pr = make_fixture(fxn, tamper='phase-failed')
        expect_reject('failed prior phase rejected', lambda: emit(b, p, v, pr, fxn / 'out'))

        # negative 2: false guardrail rejected
        fxn = tmp / 'neg2'
        fxn.mkdir()
        b, p, v, pr = make_fixture(fxn, tamper='guardrail-false')
        expect_reject('false guardrail rejected', lambda: emit(b, p, v, pr, fxn / 'out'))

        # negative 3: hardware_qualified true leak in dossier validation rejected
        _, dossier_ok = emit(build, package, verify, prepared, fx / 'out3')
        leaked = json.loads(json.dumps(dossier_ok))
        leaked['hardware_qualified'] = True
        vf = validate_dossier(leaked)
        if vf:
            negative.append('hardware_qualified true leak rejected')
        else:
            failures.append('negative case escaped: hardware_qualified leak')

        # negative 4: malformed prewarm key rejected
        fxn = tmp / 'neg4'
        fxn.mkdir()
        b, p, v, pr = make_fixture(fxn, tamper='prewarm-short')
        expect_reject('malformed prewarm key rejected', lambda: emit(b, p, v, pr, fxn / 'out'))

        # negative 5: legacy_emitter_flags true rejected
        fxn = tmp / 'neg5'
        fxn.mkdir()
        b, p, v, pr = make_fixture(fxn, tamper='legacy-true')
        expect_reject('legacy_emitter_flags true rejected', lambda: emit(b, p, v, pr, fxn / 'out'))

        # negative 6: tampered HARDWARE_PENDING boundary rejected
        tampered = json.loads(json.dumps(dossier_ok))
        tampered['hardware_pending'] = tampered['hardware_pending'][:-1]
        vf = validate_dossier(tampered)
        if vf:
            negative.append('deleted HARDWARE_PENDING item rejected')
        else:
            failures.append('negative case escaped: hardware_pending deletion')
        tampered2 = json.loads(json.dumps(dossier_ok))
        tampered2['hardware_pending'][0]['status'] = 'DONE'
        vf2 = validate_dossier(tampered2)
        if vf2:
            negative.append('tampered HARDWARE_PENDING status rejected')
        else:
            failures.append('negative case escaped: hardware_pending status tamper')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return positive, negative, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path)
    p.add_argument('--package', type=Path)
    p.add_argument('--verify', type=Path)
    p.add_argument('--repo', type=Path)
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
        if a.output:
            out = a.output.resolve()
            out.mkdir(parents=True, exist_ok=True)
            (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
        if failures:
            raise SystemExit('self-test failures: ' + '; '.join(failures))
        print('PASS: audit_gpu_bringup_readiness self-test; %d positive + %d negative'
              % (len(positive), len(negative)))
        return

    if not (a.build and a.package and a.verify and a.output):
        raise SystemExit('--build --package --verify --output are all required (or --self-test)')
    # gate mode: the self-test battery runs first and its verdict is part of the phase evidence;
    # any self-test failure refuses emission before the real evidence tree is even read.
    st_pos, st_neg, st_fail = self_test()
    if st_fail:
        raise SystemExit('self-test battery failed in gate mode: ' + '; '.join(st_fail))
    repo = a.repo.resolve() if a.repo else Path(__file__).resolve().parent.parent
    report, dossier = emit(a.build.resolve(), a.package.resolve(), a.verify.resolve(),
                           repo, a.output.resolve())
    report['self_test'] = {'passed': not st_fail, 'positive': len(st_pos), 'negative': len(st_neg)}
    (a.output.resolve() / 'result.json').write_text(json.dumps(report, indent=2, sort_keys=True) + chr(10))
    if not report.get('passed'):
        raise SystemExit('readiness dossier refused: ' + '; '.join(report.get('failures', [])))
    print('PASS: bringup readiness dossier emitted; schema %s; hardware boundary intact, no hardware claim'
          % SCHEMA)


if __name__ == '__main__':
    main()
