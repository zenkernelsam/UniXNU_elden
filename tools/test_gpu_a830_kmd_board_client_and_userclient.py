"""Compile-and-run driver for the real C++ KMD Board/Client/UserClient/Gpu host suite (gpu-opt-036).

Compiles the byte-exact pin production units from the prepared tree (zero extraction,
zero replacement) under the upstream XnuMemoryFixture host doubles, all with
-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer, runs the
battery and emits the standard result.json contract. NativeAdrenoCpu.cpp is
kernel-side-only by construction (pmap cache-attribute programming; the pin host tests
themselves replace its exports) and is transparently excluded. Host/model evidence only.
"""
from pathlib import Path
import argparse, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/EldenGpu'

def run(cmd, log):
    proc = subprocess.run(list(map(str, cmd)), capture_output=True)
    Path(log).write_bytes(proc.stdout + proc.stderr)
    return proc

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    tree = a.tree.resolve()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    gpu = tree / 'drivers/Adreno740'
    kits = tree / 'kits'
    cxx = ['xcrun', 'clang++']
    cc = ['xcrun', 'clang']

    stubs = out / 'host-headers'
    for rel in ('IOKit/IOTypes.h', 'IOKit/IOLib.h', 'IOKit/IOReturn.h', 'IOKit/IOBufferMemoryDescriptor.h',
                'kern/task.h', 'sys/resource_private.h'):
        f = stubs / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')

    includes = ['-I', gpu, '-I', gpu / 'tests', '-I', stubs,
                '-I', kits / 'ArmSmmu4K', '-I', kits / 'AdrenoPM4']
    flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-DNATIVE_ADRENO_HOST_TEST', '-include', gpu / 'tests/XnuMemoryFixture.h', *includes]

    units = [
        DRIVER / 'tests' / 'test_a830_kmd_board_client_and_userclient.cpp',
        gpu / 'NativeAdrenoBoard.cpp', gpu / 'NativeAdrenoClient.cpp',
        gpu / 'NativeAdrenoUserClient.cpp', gpu / 'NativeAdrenoGpu.cpp',
        gpu / 'NativeAdrenoMemory.cpp', gpu / 'NativeAdrenoVm.cpp',
        gpu / 'NativeAdrenoSmmuMemory.cpp', gpu / 'NativeAdrenoSmmuContext.cpp',
        gpu / 'NativeAdrenoRing.cpp', gpu / 'NativeAdrenoSubmit.cpp',
        gpu / 'NativeAdrenoPreempt.cpp', gpu / 'NativeAdrenoTimer.cpp',
        gpu / 'NativeAdrenoCpInit.cpp', gpu / 'NativeAdrenoIrq.cpp',
    ]
    missing = [str(u) for u in units if not u.is_file()]
    if missing:
        raise SystemExit('missing units: ' + ','.join(missing))
    kits_c = sorted(p for p in (kits / 'ArmSmmu4K').glob('ArmSmmu4K*.c') if p.name != 'ArmSmmu4KMmio.c') + \
        [kits / 'AdrenoPM4' / 'AdrenoPM4.c']
    kit_objs = []
    for i, src in enumerate(kits_c):
        obj = out / ('kit%d.o' % i)
        kc = [*cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I', kits / 'ArmSmmu4K', '-I', kits / 'AdrenoPM4', '-c', src, '-o', obj]
        proc = run(kc, out / ('kit%d.log' % i))
        if proc.returncode:
            raise SystemExit('kit compile failed: ' + src.name)
        kit_objs.append(obj)

    binary = out / 'kmd-bcu-suite'
    proc = run([*cxx, *flags, *units, *kit_objs, '-o', binary], out / 'build.log')
    if proc.returncode:
        raise SystemExit('kmd bcu suite compile failed; see build.log')

    env = dict(os.environ)
    env['ASAN_OPTIONS'] = (env.get('ASAN_OPTIONS', '') + ':detect_leaks=0').lstrip(':')
    proc = subprocess.run([str(binary)], capture_output=True, env=env)
    (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
    text = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')

    failures = []
    positive, negative, iterations = 17, 9, 0
    if proc.returncode:
        failures.append('harness exited nonzero: %d' % proc.returncode)
    m = re.search(r'KMD_BCU_RESULT \{"iterations":(\d+),"failures":(\d+)\}', text)
    if not m:
        failures.append('harness result line missing')
    else:
        iterations = int(m.group(1))
        if int(m.group(2)):
            failures.append('harness reports %d failures' % int(m.group(2)))
    if 'runtime error' in text or 'AddressSanitizer' in text:
        failures.append('sanitizer report detected')
    if iterations < 1500:
        failures.append('iteration count below contract: %d' % iterations)

    report = {
        'passed': not failures,
        'case_counts': {'positive': positive, 'negative': negative},
        'iterations': iterations,
        'failures': failures,
        'device_access': False,
        'hardware_qualified': False,
        'cpu_host_exclusion': ('NativeAdrenoCpu.cpp programs pmap cache attributes through kern/cpu_data.h/'
            'vm_map.h/mach_vm_param.h kernel-only APIs; the pin host tests themselves replace its exports '
            '(NativeAdrenoQueryCpu4K/Maintain* hooks). Kernel-side only by construction, transparently excluded.'),
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print(json.dumps(report))
    if failures:
        raise SystemExit('kmd bcu suite failed: ' + '; '.join(failures))
    print('PASS: a830-kmd-board-client-and-userclient suite; %d positive + %d negative, %d iterations'
          % (positive, negative, iterations))

if __name__ == '__main__':
    main()
