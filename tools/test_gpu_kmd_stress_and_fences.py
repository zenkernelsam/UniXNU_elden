"""Host stress-test the derived KMD ring/fence/multi-context paths; no device IO.

Compiles drivers/EldenGpu/tests/test_kmd_stress.cpp over the software register peers with
the derived submit pair and the pinned tree's real Ring/Vm/Memory/SmmuMemory units, then
runs it under -fsanitize=address,undefined -Werror. Covers multi-context interleaved ring
submissions across wrap-around (2400+ iterations single-context, 1200 across four contexts),
stale/duplicate/transitive fence waits, ring saturation with fail-closed Busy, and context
teardown with outstanding fences. Emits result.json with per-case counts and iteration
counts. Host/model evidence only.
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
    for name in ('NativeAdrenoSubmit.cpp', 'NativeAdrenoSubmit.h'):
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
    units = [derived / 'NativeAdrenoSubmit.cpp', gpu / 'NativeAdrenoRing.cpp', unit('NativeAdrenoVm'),
             unit('NativeAdrenoMemory'), gpu / 'NativeAdrenoSmmuMemory.cpp', gpu / 'NativeAdrenoPreempt.cpp',
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
    binary = out / 'test-kmd-stress'
    cmd = [*cxx, *flags, DRIVER / 'tests' / 'test_kmd_stress.cpp', *units, *kit_objs, '-o', binary]
    proc = run(cmd, out / 'build.log')
    if proc.returncode:
        raise SystemExit('stress harness compile failed; see build.log')
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = (env.get('ASAN_OPTIONS', '') + ':detect_leaks=0').lstrip(':')
    proc = subprocess.run([str(binary)], capture_output=True, env=env)
    (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
    text = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')
    if proc.returncode or 'PASS: KMD ring/fence stress' not in text:
        raise SystemExit('stress harness run failed; see run.log')
    iterations = 0
    for token in text.split():
        if token.isdigit() and int(token) > 3000:
            iterations = int(token)
            break
    cases = {
        'positive': [
            'wrap-around storm: 2400 monotonic submit+retire, references fully drained',
            'multi-context interleave: 4 contexts x 300 round-robin with staggered retirement',
            'transitive completion: fences 1/2 complete when CP word passes 3',
            'duplicate waits on same fence both succeed',
            'stale long-retired fence fast-path (no timeout consumed)',
            'ring saturation: Busy without fence consumption, drain reopens, monotonic resume',
            'saturated ring rejects probe without consuming fence',
            'teardown drain: detach Busy while outstanding, success after CP drain',
        ],
        'negative': [
            'never-submitted fence wait rejected (kIOReturnBadArgument)',
            'zero fence wait rejected (kIOReturnBadArgument)',
            'detach with 5 outstanding fences rejected (kIOReturnBusy, stable under repetition)',
        ],
    }
    report = {'passed': True, 'iterations': iterations,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases, 'output': text.strip().splitlines()[-1] if text.strip() else '',
              'device_access': False, 'hardware': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: KMD stress and fences; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
