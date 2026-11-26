"""Host stress-test the derived SMMU/VM address-space isolation and BO lifecycle paths.

Compiles drivers/EldenGpu/tests/test_smmu_vm_isolation.cpp over independent software SMMU
register peers (one per context) with the derived Vm/Memory pair and the pinned tree's real
SmmuMemory unit, then runs it under -fsanitize=address,undefined -Werror. Covers 900+
map/unmap/remap churn cycles, multi-context same-IOVA isolation with cross-context handle
rejection, a validation battery (zero-size/unaligned/out-of-arena/bad-permission/overlap),
double-unmap rejection, teardown with active mappings, and findAddress alignment. Emits
result.json with per-case counts and iteration counts. Host/model evidence only.
"""
from pathlib import Path
import argparse, json, os, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/EldenGpu'

def run(cmd, log):
    proc = subprocess.run(list(map(str, cmd)), capture_output=True)
    Path(log).write_bytes(proc.stdout + proc.stderr)
    return proc

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--derived', type=Path, required=True, help='Chain derived directory (NativeAdrenoSubmit pair + Vm/Memory)')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    tree = a.tree.resolve()
    derived = a.derived.resolve()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    gpu = tree / 'drivers/Adreno740'
    kits = tree / 'kits'
    for name in ('NativeAdrenoVm.cpp', 'NativeAdrenoVm.h', 'NativeAdrenoMemory.cpp', 'NativeAdrenoMemory.h'):
        if not (derived / name).is_file():
            raise SystemExit('derived pair incomplete: ' + name)
    cxx = ['xcrun', 'clang++']
    cc = ['xcrun', 'clang']
    # IOKit/kern stubs: the upstream XnuMemoryFixture supplies the doubles.
    stubs = out / 'host-headers'
    for rel in ('IOKit/IOTypes.h', 'IOKit/IOLib.h', 'IOKit/IOBufferMemoryDescriptor.h', 'kern/task.h'):
        f = stubs / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')
    # Derived headers shadow tree headers through the same -ivfsoverlay the native build uses.
    contents = [{'type': 'file', 'name': h.name, 'external-contents': str(h)}
                for h in sorted(derived.glob('*.h')) if (gpu / h.name).is_file()]
    vfs = out / 'derived-overlay.yaml'
    vfs.write_text(json.dumps({'version': 0, 'case-sensitive': False, 'fallthrough': True,
        'roots': [{'type': 'directory', 'name': str(gpu), 'contents': contents}]}, indent=2) + '\n')
    includes = ['-ivfsoverlay', vfs, '-I', derived, '-I', gpu, '-I', gpu / 'tests', '-I', stubs,
                '-I', kits / 'ArmSmmu4K', '-I', kits / 'AdrenoPM4', '-I', kits / 'AdrenoFirmware', '-I', kits / 'Sm8750Platform']
    flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-DNATIVE_ADRENO_HOST_TEST', '-include', gpu / 'tests/XnuMemoryFixture.h', *includes]
    def unit(stem):
        return derived / (stem + '.cpp') if (derived / (stem + '.cpp')).is_file() else gpu / (stem + '.cpp')
    units = [unit('NativeAdrenoVm'), unit('NativeAdrenoMemory'), gpu / 'NativeAdrenoSmmuMemory.cpp',
             DRIVER / 'tests' / 'HostKernelHooks.cpp']
    kits_c = sorted(p for p in (kits / 'ArmSmmu4K').glob('ArmSmmu4K*.c') if p.name != 'ArmSmmu4KMmio.c') + \
        [kits / 'AdrenoPM4' / 'AdrenoPM4.c', kits / 'AdrenoPM4' / 'AdrenoConstants.c']
    kit_objs = []
    for i, src in enumerate(kits_c):
        obj = out / ('kit%d.o' % i)
        kc = [*cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I', kits / 'ArmSmmu4K', '-I', kits / 'AdrenoPM4', '-c', src, '-o', obj]
        proc = run(kc, out / ('kit%d.log' % i))
        if proc.returncode:
            raise SystemExit('Kit compile failed: ' + src.name)
        kit_objs.append(obj)
    binary = out / 'test-smmu-vm-isolation'
    cmd = [*cxx, *flags, DRIVER / 'tests' / 'test_smmu_vm_isolation.cpp', *units, *kit_objs, '-o', binary]
    proc = run(cmd, out / 'build.log')
    if proc.returncode:
        raise SystemExit('isolation harness compile failed; see build.log')
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = (env.get('ASAN_OPTIONS', '') + ':detect_leaks=0').lstrip(':')
    proc = subprocess.run([str(binary)], capture_output=True, env=env)
    (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
    text = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')
    if proc.returncode or 'PASS: SMMU/VM isolation stress' not in text:
        raise SystemExit('isolation harness run failed; see run.log')
    iterations = 0
    tokens = text.replace(';', ' ').split()
    for i, token in enumerate(tokens):
        if token.isdigit() and i + 1 < len(tokens) and tokens[i + 1].startswith('iteration'):
            iterations = int(token)
            break
    cases = {
        'positive': [
            'map/unmap churn: 600 cycles fixed IOVA + 300 remap cycles walking the arena, clean detach',
            'multi-context isolation: two address spaces, same IOVA, independent mappings and teardown',
            'neighbour mapping at exact end of live mapping accepted (adjacent disjoint)',
            'validation battery leaves the VM fully usable (all rejects, then clean map/unmap)',
            'teardown with active mappings: detach succeeds after final unmap',
            'findAddress: aligned, in-arena, disjoint from live mappings',
        ],
        'negative': [
            'zero-size BO allocation rejected',
            'unaligned IOVA rejected',
            'out-of-arena IOVA rejected (below and above)',
            'bad region/writeback/shareability rejected',
            'overlapping IOVA rejected (exact base, interior, tail-spanning)',
            'cross-context handle unmap rejected both ways',
            'double-unmap rejected (stale handle)',
            'detach with active mappings Busy, stable under repetition',
            'findAddress zero-size and sub-page alignment rejected',
        ],
    }
    report = {'passed': True, 'iterations': iterations,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases, 'output': text.strip().splitlines()[-1] if text.strip() else '',
              'device_access': False, 'hardware': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: SMMU/VM isolation; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
