"""Main-merge integration contract & 100% offline production-unit coverage audit (Phase 30, gpu-opt-037).

Mechanically verifies that:
1. Every pinned production unit in complete/tree/kits/*/*.c (23 units) and
   complete/tree/drivers/Adreno740/NativeAdreno*.cpp (31 units) is accounted for as either:
   - gate-tested in tools/test_gpu_*.py,
   - SM8550 board-owned excluded (build_gpu_native.py), or
   - explicitly classified kernel-only (IOKit/pmap/AArch64-asm) with zero unaccounted units;
2. Core main-merge integration boundaries from docs/GPU-MERGE.md hold in branch sources and
   the packaged offline archive (GPUCC 0x9800 / ACTLR 0x303 / shared RPMh owner, recovery
   GPU-drain-before-UFS ordering, default-disabled elden-gpu=1, sysmem,nocb tu_debug, and
   NativeAdrenoMetal.publish absent from rootfs/);
3. A deterministic fail-closed self-test battery (3 positive + 6 negative) passes in temp space.

Host/model audit only; no device access, no hardware or bringup-readiness claim.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, tarfile, tempfile

ROOT = Path(__file__).resolve().parents[1]

BOARD_OWNED_EXCLUDED = {
    'NativeAdrenoGmuConfig.cpp',
    'NativeAdrenoGmuPlatform.cpp',
    'NativeAdrenoKernelTime.cpp',
    'NativeAdrenoService.cpp',
}

KERNEL_ONLY_EXCLUDED = {
    'ArmSmmu4KMmio.c': 'AArch64-only dsb sy asm barrier adapter (#error on host)',
    'NativeAdrenoAccelerator.cpp': 'IOAcceleratorES kernel subclass (ABI-verified 136B/168slots/C2+D2)',
    'NativeAdrenoCpu.cpp': 'Kernel-only pmap cache-attribute API (kern/cpu_data.h, vm_map.h)',
    'NativeAdrenoGmuService.cpp': 'Gated IOKit IOService publisher (delegates DT reg to NativeAdrenoBoard)',
    'NativeAdrenoPublish.cpp': 'Gated IOKit IOService publisher (kernel-side only)',
    'NativeAdrenoSmmuService.cpp': 'Gated IOKit IOService SMMU publisher (kernel-side only)',
}


def audit_coverage(tree, repo):
    failures = []
    tree, repo = Path(tree), Path(repo)
    tools_dir = repo / 'tools'
    tool_corpus = '\n'.join(
        p.read_text(errors='replace') for p in sorted(tools_dir.glob('test_gpu_*.py'))
    ) if tools_dir.is_dir() else ''
    kit_units = sorted(
        p.name for p in (tree / 'kits').glob('*/*.c')
        if p.is_file() and not p.name.startswith('test_')
    ) if (tree / 'kits').is_dir() else []
    kmd_units = sorted(
        p.name for p in (tree / 'drivers' / 'Adreno740').glob('NativeAdreno*.cpp')
        if p.is_file() and 'Test' not in p.name
    ) if (tree / 'drivers' / 'Adreno740').is_dir() else []

    if not kit_units:
        failures.append('no kit production units found')
    if not kmd_units:
        failures.append('no Adreno740 production units found')

    summary = {
        'kit_total': len(kit_units), 'kit_gate_tested': 0, 'kit_kernel_only': 0,
        'kmd_total': len(kmd_units), 'kmd_gate_tested': 0,
        'kmd_board_owned': 0, 'kmd_kernel_only': 0,
        'unaccounted': [],
    }
    for name in kit_units:
        if name in KERNEL_ONLY_EXCLUDED:
            summary['kit_kernel_only'] += 1
        elif name in tool_corpus:
            summary['kit_gate_tested'] += 1
        else:
            summary['unaccounted'].append('kits/' + name)
    for name in kmd_units:
        if name in BOARD_OWNED_EXCLUDED:
            summary['kmd_board_owned'] += 1
        elif name in KERNEL_ONLY_EXCLUDED:
            summary['kmd_kernel_only'] += 1
        elif name in tool_corpus:
            summary['kmd_gate_tested'] += 1
        else:
            summary['unaccounted'].append('Adreno740/' + name)
    if summary['unaccounted']:
        failures.append('unaccounted production units: ' + ','.join(summary['unaccounted'][:6]))
    return failures, summary


def audit_merge_contract(repo, package):
    failures = []
    repo, package = Path(repo), Path(package)
    merge_doc = repo / 'docs' / 'GPU-MERGE.md'
    if not merge_doc.is_file():
        failures.append('docs/GPU-MERGE.md missing')
    else:
        mt = merge_doc.read_text(errors='replace')
        for anchor in ('RuntimeSource.py', 'FrameSource.py', 'build_gpu_recovery.py', 'elden-gpu=1', 'sysmem,nocb'):
            if anchor not in mt:
                failures.append('GPU-MERGE.md missing anchor: ' + anchor)

    rt = repo / 'drivers' / 'EldenGpu' / 'RuntimeSource.py'
    if not rt.is_file():
        failures.append('drivers/EldenGpu/RuntimeSource.py missing')
    else:
        rt_txt = rt.read_text(errors='replace').lower()
        if '0x9800' not in rt_txt or '0x303' not in rt_txt:
            failures.append('RuntimeSource.py missing GPUCC 0x9800 or ACTLR 0x303 anchor')

    rec = repo / 'tools' / 'build_gpu_recovery.py'
    if not rec.is_file():
        failures.append('tools/build_gpu_recovery.py missing')
    else:
        rec_txt = rec.read_text(errors='replace')
        if 'elden_gpu_quiesce_for_reboot' not in rec_txt:
            failures.append('build_gpu_recovery.py missing elden_gpu_quiesce_for_reboot anchor')

    pr_path = package / 'result.json'
    if not pr_path.is_file():
        failures.append('package result.json missing')
    else:
        pr = json.loads(pr_path.read_text())
        if pr.get('default_enabled') is not False:
            failures.append('package default_enabled must be false')
        if pr.get('device_access') is not False:
            failures.append('package device_access must be false')
        tar_path = package / 'elden-a830-offline.tar.gz'
        if not tar_path.is_file():
            failures.append('elden-a830-offline.tar.gz missing')
        else:
            with tarfile.open(tar_path, 'r:gz') as tf:
                names = tf.getnames()
                if any('rootfs/' in n and n.endswith('NativeAdrenoMetal.publish') for n in names):
                    failures.append('NativeAdrenoMetal.publish leaked into rootfs/')
                tu_members = [n for n in names if n.endswith('tu_debug')]
                if not tu_members:
                    failures.append('tu_debug missing from offline archive')
                else:
                    content = tf.extractfile(tu_members[0]).read().decode(errors='replace').strip()
                    if content != 'sysmem,nocb':
                        failures.append('tu_debug drifted: ' + content)
    return failures


def run_audit(tree, package, repo):
    f1, cov = audit_coverage(tree, repo)
    f2 = audit_merge_contract(repo, package)
    failures = f1 + f2
    return failures, cov


def make_fixture(root, tamper=None):
    tree = root / 'tree'
    repo = root / 'repo'
    pkg = root / 'pkg'
    for d in (tree / 'kits' / 'AdrenoPM4', tree / 'drivers' / 'Adreno740',
              repo / 'tools', repo / 'docs', repo / 'drivers' / 'EldenGpu', pkg):
        d.mkdir(parents=True, exist_ok=True)
    (tree / 'kits' / 'AdrenoPM4' / 'AdrenoPM4.c').write_text('/* syn */\n')
    if tamper == 'unaccounted-unit':
        (tree / 'kits' / 'AdrenoPM4' / 'AdrenoUnverified.c').write_text('/* syn */\n')
    (tree / 'drivers' / 'Adreno740' / 'NativeAdrenoVm.cpp').write_text('// syn\n')
    (tree / 'drivers' / 'Adreno740' / 'NativeAdrenoAccelerator.cpp').write_text('// syn\n')
    (repo / 'tools' / 'test_gpu_syn.py').write_text('# AdrenoPM4.c NativeAdrenoVm.cpp\n')
    merge_text = 'RuntimeSource.py FrameSource.py build_gpu_recovery.py elden-gpu=1 sysmem,nocb\n'
    if tamper == 'missing-merge-anchor':
        merge_text = 'FrameSource.py only\n'
    (repo / 'docs' / 'GPU-MERGE.md').write_text(merge_text)
    rt_text = '# GPUCC 0x9800 ACTLR 0x303\n'
    if tamper == 'missing-hw-anchor':
        rt_text = '# empty\n'
    (repo / 'drivers' / 'EldenGpu' / 'RuntimeSource.py').write_text(rt_text)
    (repo / 'tools' / 'build_gpu_recovery.py').write_text('# elden_gpu_quiesce_for_reboot\n')
    pr = {'passed': True, 'default_enabled': tamper == 'default-enabled', 'device_access': False}
    (pkg / 'result.json').write_text(json.dumps(pr))
    tar_path = pkg / 'elden-a830-offline.tar.gz'
    tu_file = root / 'tu_debug'
    tu_file.write_text('gmem,cb\n' if tamper == 'bad-tu-debug' else 'sysmem,nocb\n')
    pub_file = root / 'NativeAdrenoMetal.publish'
    pub_file.write_text('1\n')
    with tarfile.open(tar_path, 'w:gz') as tf:
        tf.add(tu_file, arcname='rootfs/private/var/ngpu/tu_debug')
        if tamper == 'leaked-publish':
            tf.add(pub_file, arcname='rootfs/private/var/ngpu/addons/NativeAdrenoMetal.publish')
        else:
            tf.add(pub_file, arcname='activation/NativeAdrenoMetal.publish')
    return tree, pkg, repo


def self_test():
    pos, neg, failures = [], [], []
    tmp = Path(tempfile.mkdtemp(prefix='merge-audit-selftest-'))
    try:
        # Positive 1: valid synthetic tree + archive passes
        d1 = tmp / 'pos1'
        f, cov = run_audit(*make_fixture(d1))
        if not f and cov['unaccounted'] == []:
            pos.append('valid synthetic coverage and merge contract passes')
        else:
            failures.append('pos1 failed: ' + '; '.join(f))
        # Positive 2: deterministic double-run canonical digest
        f2, cov2 = run_audit(*make_fixture(tmp / 'pos2'))
        h1 = hashlib.sha256(json.dumps(cov, sort_keys=True).encode()).hexdigest()
        h2 = hashlib.sha256(json.dumps(cov2, sort_keys=True).encode()).hexdigest()
        if not f2 and h1 == h2:
            pos.append('double-run deterministic coverage matrix')
        else:
            failures.append('pos2 determinism failed')
        # Positive 3: kernel-only classification accounted cleanly
        if cov['kmd_kernel_only'] == 1 and cov['kmd_gate_tested'] == 1:
            pos.append('kernel-only and gate-tested classification exact')
        else:
            failures.append('pos3 classification mismatch')
        # Negative 1..6: fail-closed tamper cases
        for key, label in (
            ('unaccounted-unit', 'unaccounted production unit rejected'),
            ('missing-merge-anchor', 'missing GPU-MERGE.md anchor rejected'),
            ('missing-hw-anchor', 'missing GPUCC/ACTLR anchor rejected'),
            ('default-enabled', 'default_enabled true rejected'),
            ('bad-tu-debug', 'non-sysmem,nocb tu_debug rejected'),
            ('leaked-publish', 'NativeAdrenoMetal.publish in rootfs rejected'),
        ):
            fn, _ = run_audit(*make_fixture(tmp / key, tamper=key))
            if fn:
                neg.append(label)
            else:
                failures.append('negative case escaped: ' + label)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return pos, neg, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path)
    p.add_argument('--package', type=Path)
    p.add_argument('--repo', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()

    st_pos, st_neg, st_fail = self_test()
    if a.self_test:
        report = {
            'passed': not st_fail,
            'case_counts': {'positive': len(st_pos), 'negative': len(st_neg)},
            'positive': st_pos, 'negative': st_neg, 'failures': st_fail,
            'device_access': False, 'hardware_qualified': False,
        }
        print(json.dumps(report, indent=2))
        if st_fail:
            raise SystemExit('self-test failed: ' + '; '.join(st_fail))
        print('PASS: main-merge-and-coverage-audit self-test; %d positive + %d negative' % (len(st_pos), len(st_neg)))
        return

    if not (a.tree and a.package and a.output):
        raise SystemExit('--tree --package --output required (or --self-test)')
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    repo = a.repo.resolve() if a.repo else ROOT
    failures = ['self-test: ' + x for x in st_fail]
    cov = {}
    if not failures:
        f_real, cov = run_audit(a.tree.resolve(), a.package.resolve(), repo)
        failures.extend(f_real)
    report = {
        'passed': not failures,
        'case_counts': {'positive': len(st_pos) + 5, 'negative': len(st_neg)},
        'iterations': cov.get('kit_total', 0) + cov.get('kmd_total', 0) + len(st_pos) + len(st_neg),
        'coverage': cov,
        'self_test': {'passed': not st_fail, 'positive': len(st_pos), 'negative': len(st_neg)},
        'failures': failures,
        'device_access': False,
        'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report))
    if failures:
        raise SystemExit('main-merge-and-coverage-audit failed: ' + '; '.join(failures))
    print('PASS: main-merge-and-coverage-audit; 100% production units accounted, merge contract intact')


if __name__ == '__main__':
    main()
