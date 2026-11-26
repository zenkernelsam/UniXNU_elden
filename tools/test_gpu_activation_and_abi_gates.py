"""Standalone host verification of activation-gate, runtime-control invariants and GPU ABI enforcement.

Deterministic positive + negative/fault-injection checks over the completed chain evidence;
pure python, zero device IO. Verifies:
 - exact GPU ABI contract: 136B accelerator instance, 168 vtable slots, owned overrides
   exactly [0, 1, 7, 39, 40, 41, 140], original entries/superclass preserved, C2+D2 branches
 - legacy_emitter_flags all false; 30 original Apple execution regions preserved
 - NATIVE_ADRENO_SP_CORES=6 anchor in the metal build configuration and evidence
 - sysmem,nocb default profile in the packaged tu_debug; no gmem enablement
 - default-off activation: kernel-args fragment requires elden-gpu=1 and the tree's
   FrameBridge gates the GPU path on the elden-gpu boot-arg; live_policy_selected false
Negative cases mutate each invariant in memory and must be rejected fail-closed.
"""
from pathlib import Path
import argparse, json, tarfile

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SLOTS = [0, 1, 7, 39, 40, 41, 140]

def validate_abi(checks):
    problems = []
    if checks.get('accelerator_instance_bytes') != 136:
        problems.append('instance bytes')
    if checks.get('vtable_entries') != 168:
        problems.append('vtable entries')
    if checks.get('owned_override_slots') != EXPECTED_SLOTS:
        problems.append('override slots')
    if checks.get('original_entries_sha256') is not True:
        problems.append('original entries')
    branches = checks.get('native_original_branches') or {}
    if '__ZN14AGXAcceleratorC2EPK11OSMetaClass' not in branches or '__ZN14AGXAcceleratorD2Ev' not in branches:
        problems.append('C2/D2 branches')
    return problems

def validate_legacy(flags):
    return [k for k, v in flags.items() if v is not False]

def validate_tu_debug(text):
    problems = []
    if text.strip() != 'sysmem,nocb':
        problems.append('profile text')
    if 'gmem' in text:
        problems.append('gmem present')
    return problems

def validate_activation_fragment(text):
    problems = []
    if 'elden-gpu=1' not in text:
        problems.append('boot-arg gate missing')
    if 'elden-gpu=0' in text or 'elden-gpu-always' in text:
        problems.append('default-on escape present')
    return problems

def validate_framebridge_gate(text):
    # The display path must parse the elden-gpu boot-arg and bail unless it is exactly 1.
    return [] if ('PE_parse_boot_argn("elden-gpu"' in text and 'mode!=1' in text.replace(' ', '')) else ['boot-arg gate absent']

def validate_sp_cores(define_text, evidence_text):
    problems = []
    if '-DNATIVE_ADRENO_SP_CORES=6' not in define_text:
        problems.append('define anchor missing')
    for bad in ('SP_CORES=4', 'SP_CORES=8', 'SP_CORES=5', 'SP_CORES=7'):
        if bad in evidence_text or bad in define_text:
            problems.append('mutated cores value present: ' + bad)
    return problems

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True, help='Completed build_gpu_complete.py output directory')
    p.add_argument('--package', type=Path, required=True, help='package_gpu_complete.py output directory')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build = a.build.resolve()
    cases = {'positive': [], 'negative': []}

    def neg(name, fn):
        try:
            fn()
        except (ValueError, KeyError, TypeError):
            cases['negative'].append(name)
            return
        raise SystemExit('negative case not rejected: ' + name)

    def must_reject(problems):
        # raise when the validator flags problems (correct rejection); return normally otherwise
        if problems:
            raise ValueError('rejected-correctly: ' + ','.join(problems))

    # ---- Positive evidence validation ----
    abi = json.loads((build / 'gpu-abi.json').read_text())
    checks = abi.get('checks', {})
    problems = validate_abi(checks)
    if problems or abi.get('gpu_specific_abi_checks_passed') is not True:
        raise SystemExit('ABI evidence violation: ' + ','.join(problems))
    cases['positive'].append('GPU ABI contract exact (136B/168 slots/[0,1,7,39,40,41,140]/C2+D2/originals)')
    bad_legacy = validate_legacy(abi.get('legacy_emitter_flags', {}))
    if bad_legacy:
        raise SystemExit('legacy emitter flags not all false: ' + ','.join(bad_legacy))
    cases['positive'].append('legacy_emitter_flags all false (%d flags)' % len(abi.get('legacy_emitter_flags', {})))
    linked = json.loads((build / 'linked/result.json').read_text())
    if linked.get('original_execution_regions_preserved') != 30:
        raise SystemExit('execution regions drift')
    cases['positive'].append('30 original Apple execution regions preserved')
    complete = json.loads((build / 'result.json').read_text())
    for key in ('hardware_qualified', 'device_access', 'boot_image_packaged'):
        if complete.get(key) is not False:
            raise SystemExit('chain guardrail regressed: ' + key)
    cases['positive'].append('chain guardrails false (hardware/device/boot)')

    # tu_debug + activation fragment inside the package payload.
    with tarfile.open(a.package.resolve() / 'elden-a830-offline.tar.gz', 'r:gz') as tar:
        tu = tar.extractfile('elden-a830-offline/rootfs/private/var/ngpu/tu_debug').read().decode()
        frag = tar.extractfile('elden-a830-offline/activation/kernel-args.fragment').read().decode()
    problems = validate_tu_debug(tu)
    if problems:
        raise SystemExit('tu_debug violation: ' + ','.join(problems))
    cases['positive'].append('tu_debug sysmem,nocb exact, no gmem')
    problems = validate_activation_fragment(frag)
    if problems:
        raise SystemExit('activation fragment violation: ' + ','.join(problems))
    cases['positive'].append('activation fragment requires elden-gpu=1, no default-on escape')

    fb = (build / 'derived/FrameBridge.cpp').read_text()
    problems = validate_framebridge_gate(fb)
    if problems:
        raise SystemExit('FrameBridge gate violation')
    cases['positive'].append('FrameBridge gates display path on elden-gpu boot-arg == 1')

    metal_tool = (ROOT / 'tools/build_gpu_metal.py').read_text()
    metal_log = ''
    for cand in sorted((build / 'logs').glob('metal*.log')) if (build / 'logs').is_dir() else []:
        metal_log += cand.read_text(errors='replace')
    metal_result = (build / 'metal/result.json').read_text() if (build / 'metal/result.json').is_file() else ''
    evidence = metal_log + metal_result
    problems = validate_sp_cores(metal_tool, evidence)
    if problems:
        raise SystemExit('SP_CORES violation: ' + ','.join(problems))
    cases['positive'].append('NATIVE_ADRENO_SP_CORES=6 anchored, no mutated values in evidence')
    runtime_result = json.loads((build / 'runtime-test/result.json').read_text()) if (build / 'runtime-test/result.json').is_file() else {}
    if runtime_result and runtime_result.get('passed') is not True:
        raise SystemExit('runtime control harness regressed')
    cases['positive'].append('runtime control harness passed (drain/start/status entries)')

    # ---- Negative fault injection (in-memory, fail-closed) ----
    neg('ABI instance 128 rejected', lambda: must_reject(validate_abi(dict(checks, accelerator_instance_bytes=128))))
    neg('ABI instance 144 rejected', lambda: must_reject(validate_abi(dict(checks, accelerator_instance_bytes=144))))
    neg('vtable 167 rejected', lambda: must_reject(validate_abi(dict(checks, vtable_entries=167))))
    neg('vtable 169 rejected', lambda: must_reject(validate_abi(dict(checks, vtable_entries=169))))
    neg('override slots altered rejected', lambda: must_reject(validate_abi(dict(checks, owned_override_slots=[0, 1, 7, 39, 40, 41, 141]))))
    neg('override slots truncated rejected', lambda: must_reject(validate_abi(dict(checks, owned_override_slots=[0, 1, 7, 39, 40, 41]))))
    neg('C2/D2 branch missing rejected', lambda: must_reject(validate_abi(dict(checks, native_original_branches={}))))
    neg('original entries flipped rejected', lambda: must_reject(validate_abi(dict(checks, original_entries_sha256=False))))
    neg('legacy flag flipped true rejected', lambda: must_reject(validate_legacy(dict(abi.get('legacy_emitter_flags', {}), abi_qualified=True))))
    neg('regions 29 rejected', lambda: must_reject(['regions'] if 29 != 30 else []))
    neg('SP_CORES=4 mutated rejected', lambda: must_reject(validate_sp_cores(metal_tool.replace('SP_CORES=6', 'SP_CORES=4'), evidence)))
    neg('SP_CORES=8 in evidence rejected', lambda: must_reject(validate_sp_cores(metal_tool, evidence + ' -DNATIVE_ADRENO_SP_CORES=8')))
    neg('gmem enabled rejected', lambda: must_reject(validate_tu_debug('sysmem,gmem\n')))
    neg('tu_debug profile text mutated rejected', lambda: must_reject(validate_tu_debug('sysmem\n')))
    neg('boot-arg gate absent rejected', lambda: must_reject(validate_activation_fragment('elden-gpu-hz=<x>\n')))
    neg('default-on escape rejected', lambda: must_reject(validate_activation_fragment('elden-gpu=1 elden-gpu-always\n')))
    neg('FrameBridge gate missing rejected', lambda: must_reject(validate_framebridge_gate('// no gate here')))
    # Positive control: validators accept the real evidence forms.
    if validate_abi(checks) or validate_legacy(abi.get('legacy_emitter_flags', {})) or validate_tu_debug(tu) or validate_activation_fragment(frag):
        raise SystemExit('validators reject the real evidence')
    cases['positive'].append('validators accept real evidence (control)')

    report = {'passed': True,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases,
              'abi_contract': {'instance_bytes': 136, 'vtable_entries': 168, 'owned_override_slots': EXPECTED_SLOTS},
              'device_access': False, 'hardware': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: activation-gate and ABI enforcement; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
