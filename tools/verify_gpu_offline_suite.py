"""Unified offline host verification gate for the Elden GPU chain.

Runs the standalone host ASan/UBSan suites sequentially against a completed
build_gpu_complete.py output and validates the chain/package guardrails, then
emits a single machine-readable offline-verification.json. Verification only;
no device IO, no live frequency/voltage policy, no hardware claims.
"""
from pathlib import Path
import argparse, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]

def tool(name):
    return str(ROOT / 'tools' / name)

def load(path):
    return json.loads(Path(path).read_text())

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True, help='Completed build_gpu_complete.py output directory')
    p.add_argument('--package', type=Path, required=True, help='package_gpu_complete.py output directory')
    p.add_argument('--mesa-pristine', type=Path, required=True, help='Pristine Mesa cache for the patch verification')
    p.add_argument('--linux', type=Path, required=True, help='TB322FC kernel clone for the DTS parity phase')
    p.add_argument('--prepared', type=Path, required=True, help='Prepared inputs tree used by the chain (KMD diagnostics sources)')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    build = a.build.resolve()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tree = build / 'tree'
    phases = {}

    def run_phase(name, argv):
        phase_out = out / name
        cmd = [sys.executable] + [str(x) for x in argv] + ['--output', phase_out]
        proc = subprocess.run(cmd, capture_output=True)
        (out / (name + '.log')).write_bytes(proc.stdout + proc.stderr)
        entry = {'returncode': proc.returncode, 'passed': proc.returncode == 0}
        result_file = phase_out / 'result.json'
        if result_file.is_file():
            entry['result'] = load(result_file)
        phases[name] = entry
        return proc.returncode == 0

    ok = True
    objdump = 'llvm-objdump'
    if sys.platform == 'darwin':
        probe = subprocess.run(['xcrun', '-f', 'llvm-objdump'], capture_output=True, text=True)
        if probe.returncode == 0 and probe.stdout.strip():
            objdump = probe.stdout.strip()
    ok &= run_phase('board-inputs', [tool('test_gpu_board_inputs.py'), '--tree', tree,
                    '--generated', build / 'opps', '--linux', a.linux.resolve(), '--objdump', objdump])
    ok &= run_phase('submit-timing', [tool('test_gpu_submit_timing.py'), '--tree', tree,
                    '--derived', build / 'derived'])
    ok &= run_phase('turnip-patches', [tool('test_gpu_turnip_patches.py'), '--tree', tree,
                    '--mesa', a.mesa_pristine.resolve()])
    ok &= run_phase('kmd-diag', [tool('test_gpu_kmd_diag.py'), '--tree', a.prepared.resolve() / 'tree'])
    ok &= run_phase('recovery-frame-diag', [tool('test_gpu_recovery_and_frame_diag.py'),
                    '--derived', build / 'derived', '--wire', tree / 'drivers/GopFramebuffer',
                    '--recovery', build / 'recovery'])
    ok &= run_phase('firmware-warnings', [tool('test_gpu_firmware_and_warnings.py'),
                    '--prepared', a.prepared.resolve(), '--native', build / 'core'])
    ok &= run_phase('userspace-package-hygiene', [tool('test_gpu_userspace_and_package_hygiene.py'),
                    '--package', a.package.resolve()])
    ok &= run_phase('activation-abi-gates', [tool('test_gpu_activation_and_abi_gates.py'),
                    '--build', build, '--package', a.package.resolve()])
    ok &= run_phase('lock-repro', [tool('test_gpu_lock_and_repro.py'),
                    '--prepared', a.prepared.resolve(), '--build', build])
    ok &= run_phase('kmd-stress-fences', [tool('test_gpu_kmd_stress_and_fences.py'),
                    '--tree', tree, '--derived', build / 'derived'])
    ok &= run_phase('smmu-vm-isolation', [tool('test_gpu_smmu_vm_isolation.py'),
                    '--tree', tree, '--derived', build / 'derived'])
    ok &= run_phase('gmu-hfi-framing', [tool('test_gpu_gmu_hfi_framing.py'),
                    '--tree', tree])
    ok &= run_phase('pm4-cmdbuf-bounds', [tool('test_gpu_pm4_cmdbuf_bounds.py'),
                    '--tree', tree, '--derived', build / 'derived'])
    ok &= run_phase('m2v-callsite-reachability', [tool('test_gpu_m2v_callsite_reachability.py'),
                    '--tree', tree, '--m2v-repo', str(Path(a.build).resolve().parent / 'caches' / 'metal2vulkan')])
    ok &= run_phase('rpmh-votes-and-callgate', [tool('test_gpu_rpmh_votes_and_callgate.py'),
                    '--tree', tree, '--prepared', a.prepared.resolve(),
                    '--opps', build / 'opps', '--derived', build / 'derived'])
    ok &= run_phase('runtime-lifecycle-and-maintenance', [tool('test_gpu_runtime_lifecycle_and_maintenance.py'),
                    '--derived', build / 'derived'])
    ok &= run_phase('userspace-icd-and-abi-symbols', [tool('test_gpu_userspace_icd_and_abi_symbols.py'),
                    '--build', build, '--package', a.package.resolve(), '--tree', tree])
    ok &= run_phase('a830-hwinit-irq-and-sha256', [tool('test_gpu_a830_hwinit_irq_and_sha256.py'),
                    '--prepared', a.prepared.resolve()])
    ok &= run_phase('a830-hwinit-regseq-parity', [tool('test_gpu_a830_hwinit_regseq_parity.py'),
                    '--prepared', a.prepared.resolve()])
    ok &= run_phase('a830-power-zap-and-timer', [tool('test_gpu_a830_power_zap_and_timer.py'),
                    '--prepared', a.prepared.resolve()])
    ok &= run_phase('pre-bringup-contract', [tool('test_gpu_pre_bringup_contract.py'),
                    '--tree', tree, '--package', a.package.resolve(), '--prepared', a.prepared.resolve()])

    # Guardrail validation across the chain and package evidence.
    guardrails = {}
    complete = load(build / 'result.json')
    abi = load(build / 'gpu-abi.json')
    core = load(build / 'core' / 'result.json')
    linked = load(build / 'linked' / 'result.json')
    package = load(a.package.resolve() / 'result.json')
    guardrails['hardware_qualified_false'] = complete.get('hardware_qualified') is False
    guardrails['device_access_false'] = complete.get('device_access') is False
    guardrails['boot_image_packaged_false'] = complete.get('boot_image_packaged') is False
    guardrails['resumed_userspace_false'] = complete.get('resumed_userspace') is False
    guardrails['complete_offline_build'] = complete.get('complete_offline_build') is True
    guardrails['gpu_specific_abi_checks_passed'] = abi.get('gpu_specific_abi_checks_passed') is True
    legacy = abi.get('legacy_emitter_flags') or {}
    guardrails['legacy_emitter_flags_all_false'] = bool(legacy) and all(v is False for v in legacy.values())
    guardrails['original_execution_regions_preserved_30'] = linked.get('original_execution_regions_preserved') == 30
    guardrails['core_passed'] = core.get('passed') is True
    guardrails['linked_passed'] = linked.get('passed') is True
    guardrails['package_passed'] = package.get('passed') is True
    guardrails['package_device_access_false'] = package.get('device_access') is False
    guardrails['package_default_enabled_false'] = package.get('default_enabled') is False
    guardrails['package_readback'] = package.get('archive_readback_passed') is True
    stages = load(build / 'stages.json')
    stage_rows = stages if isinstance(stages, list) else stages.get('stages', [])
    guardrails['all_20_stages_rc0'] = len(stage_rows) == 20 and all(x.get('returncode', 1) == 0 for x in stage_rows)
    # live_policy_selected must stay false in the board-inputs evidence.
    board = phases.get('board-inputs', {}).get('result') or {}
    parity = board.get('dts_parity') or {}
    guardrails['live_policy_selected_false'] = parity.get('live_policy_selected') is False
    guardrails['board_hardware_qualified_false'] = parity.get('hardware_qualified') is False

    def write_rollup(passed_so_far):
        manifest = {
            'passed': bool(passed_so_far),
            'phases': {name: {'returncode': ph['returncode'], 'passed': ph['passed']} for name, ph in phases.items()},
            'guardrails': guardrails,
            'package_archive_sha256': package.get('archive_sha256'),
            'kernel_sha256': abi.get('kernel_sha256'),
            'abi_checks': abi.get('checks'),
            'device_access': False,
            'hardware_qualified': False,
            'live_policy_selected': False,
            'note': 'Unified offline host verification; host/model evidence only, no device or hardware claim.',
        }
        (out / 'offline-verification.json').write_text(json.dumps(manifest, indent=2) + '\n')

    # Provisional rollup (phases 1-15 + guardrails) so the bringup-manifest phase can
    # consume verify/offline-verification.json without a circular dependency; the final
    # rewrite below covers all 17 phases.
    write_rollup(ok and all(guardrails.values()))
    ok &= run_phase('bringup-manifest', [tool('test_gpu_bringup_manifest_gate.py'),
                    '--build', build, '--package', a.package.resolve(), '--verify', out])
    ok &= run_phase('prepare-scratch-refs', [tool('prepare_gpu_scratch_refs.py'), '--self-test'])

    ok &= all(guardrails.values())
    write_rollup(ok)
    if not ok:
        failed = [k for k, v in guardrails.items() if not v]
        failed += [n for n, ph in phases.items() if not ph['passed']]
        raise SystemExit('Offline verification failed: ' + ','.join(failed))
    print('PASS: unified offline verification; host/model evidence only')

if __name__ == '__main__':
    main()
