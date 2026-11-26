"""Host stress-test PM4 packet emission/decoding bounds and IB command-buffer batches.

Compiles drivers/EldenGpu/tests/test_pm4_cmdbuf_bounds.cpp with the pinned tree's real
AdrenoPM4 kit unit plus the derived Ring/Submit/VM units over software SMMU/CP peers under
-fsanitize=address,undefined -Werror. Covers the Type4/Type7 emission boundary battery
(18-bit registers, 1..127 values, register-range overflow, 7-bit opcodes, 0..16383 values,
parity-bit rejection), structural Plan round-trips and truncation, multi-IB batches (2-4
buffers, 150 submissions), and ring wrap-around packet integrity across 520 submissions
with real-decoder prologue parsing. Emits result.json. Host/model evidence only.
"""
from pathlib import Path
import argparse, json, os, subprocess
ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/EldenGpu'

def run(cmd, log):
    proc = subprocess.run(list(map(str, cmd)), capture_output=True)
    Path(log).write_bytes(proc.stdout + proc.stderr)
    return proc

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--derived', type=Path, required=True)
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
    stubs = out / 'host-headers'
    for rel in ('IOKit/IOTypes.h', 'IOKit/IOLib.h', 'IOKit/IOBufferMemoryDescriptor.h', 'kern/task.h'):
        f = stubs / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')
    contents = [{'type': 'file', 'name': h.name, 'external-contents': str(h)}
                for h in sorted(derived.glob('*.h')) if (gpu / h.name).is_file()]
    vfs = out / 'derived-overlay.yaml'
    vfs.write_text(json.dumps({'version': 0, 'case-sensitive': False, 'fallthrough': True,
        'roots': [{'type': 'directory', 'name': str(gpu), 'contents': contents}]}, indent=2) + chr(10))
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
        kc = [cc[0], cc[1], '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I', kits / 'ArmSmmu4K', '-I', kits / 'AdrenoPM4', '-c', src, '-o', obj]
        proc = run(kc, out / ('kit%d.log' % i))
        if proc.returncode:
            raise SystemExit('Kit compile failed: ' + src.name)
        kit_objs.append(obj)
    binary = out / 'test-pm4-cmdbuf'
    cmd = [*cxx, *flags, DRIVER / 'tests' / 'test_pm4_cmdbuf_bounds.cpp', *units, *kit_objs, '-o', binary]
    proc = run(cmd, out / 'build.log')
    if proc.returncode:
        raise SystemExit('pm4 harness compile failed; see build.log')
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = (env.get('ASAN_OPTIONS', '') + ':detect_leaks=0').lstrip(':')
    proc = subprocess.run([str(binary)], capture_output=True, env=env)
    (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
    text = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')
    if proc.returncode or 'PASS: PM4/cmdbuf bounds' not in text:
        raise SystemExit('pm4 harness run failed; see run.log')
    iterations = 0
    tokens = text.replace(';', ' ').split()
    for i, token in enumerate(tokens):
        if token.isdigit() and i + 1 < len(tokens) and tokens[i + 1].startswith('iteration'):
            iterations = int(token)
            break
    cases = {
        'positive': [
            'Type4 boundary: max reg count-1, exact reg+count-1==0x3ffff boundary accepted',
            'Type7 boundary: max opcode 0x7f, max count 16383, zero count accepted',
            'Plan round-trip: emitted type4+type7 stream decodes with exact identifiers/counts',
            'Plan empty input succeeds with zero packets',
            'multi-IB batches: 2/3/4 buffers round-robin x 150 submissions, fences monotonic, clean drain',
            'ring wrap integrity: 520 submissions, prologue parses as valid packets by the real decoder (4+ headers)',
        ],
        'negative': [
            'Type4 reg too wide (0x40000), count 0, count 128 rejected',
            'Type4 register-range overflow (reg+count-1 past 0x3ffff) rejected',
            'Type7 opcode 0x80, count 16384 rejected',
            'parity-bit corruption: count-parity and opcode-parity flips decode InvalidHeader',
            'Plan truncated stream rejected with count reset',
            'Plan capacity overflow rejected',
        ],
    }
    report = {'passed': True, 'iterations': iterations,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases, 'output': text.strip().splitlines()[-1] if text.strip() else '',
              'device_access': False, 'hardware': False, 'hardware_qualified': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print('PASS: PM4/cmdbuf bounds; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
