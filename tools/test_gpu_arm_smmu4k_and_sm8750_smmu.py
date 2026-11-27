"""Compile-and-run driver for the real ArmSmmu4K + Sm8750GpuSmmu host suite (gpu-opt-032).

Compiles the byte-exact pin production units from the prepared tree (zero extraction,
zero replacement) as independent C11 objects plus the C++17 harness, all under
-Wall -Wextra -Werror with ASan+UBSan and -fno-omit-frame-pointer, then runs the
stress battery and emits the standard result.json contract.
"""
from pathlib import Path
import argparse, json, re, subprocess, sys

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    log = out / 'build.log'
    failures = []

    kit = a.prepared.resolve() / 'tree' / 'kits' / 'ArmSmmu4K'
    smkit = a.prepared.resolve() / 'tree' / 'kits' / 'Sm8750Platform'
    harness = Path(__file__).resolve().parent.parent / 'drivers' / 'EldenGpu' / 'tests' / 'test_arm_smmu4k_and_sm8750_smmu.cpp'
    c_units = [
        kit / 'ArmSmmu4K.c', kit / 'ArmSmmu4KAudit.c', kit / 'ArmSmmu4KContext.c',
        kit / 'ArmSmmu4KLive.c', kit / 'ArmSmmu4KProgram.c',
        kit / 'ArmSmmu4KStreams.c', kit / 'ArmSmmu4KTableMemory.c',
        smkit / 'Sm8750GpuSmmu.c',
    ]
    missing = [str(u) for u in c_units if not u.is_file()] + ([] if harness.is_file() else [str(harness)])
    if missing:
        failures.append('missing inputs: ' + ','.join(missing))

    objects = []
    if not failures:
        flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        with log.open('w') as lf:
            for unit in c_units:
                obj = out / (unit.stem + '.o')
                cmd = ['clang', '-std=c11', '-I', str(kit), '-I', str(smkit), '-c', str(unit), '-o', str(obj)] + flags
                proc = subprocess.run(cmd, stdout=lf, stderr=lf)
                if proc.returncode != 0:
                    failures.append('C unit compile failed: ' + unit.name)
                objects.append(obj)
            harness_obj = out / 'harness.o'
            cmd = ['clang++', '-std=c++17', '-I', str(kit), '-I', str(smkit), '-c', str(harness), '-o', str(harness_obj)] + flags
            proc = subprocess.run(cmd, stdout=lf, stderr=lf)
            if proc.returncode != 0:
                failures.append('harness compile failed (ASan+UBSan -Werror)')
            binary = out / 'smmu4k_suite'
            if not failures:
                proc = subprocess.run(['clang++'] + [str(o) for o in objects] + [str(harness_obj), '-o', str(binary)]
                                      + ['-fsanitize=address,undefined'], stdout=lf, stderr=lf)
                if proc.returncode != 0:
                    failures.append('link failed')

    positive, negative, iterations = 0, 0, 0
    run_log = out / 'run.log'
    if not failures:
        proc = subprocess.run([str(binary)], capture_output=True)
        run_log.write_bytes(proc.stdout + proc.stderr)
        text = (proc.stdout + proc.stderr).decode(errors='replace')
        if proc.returncode != 0:
            failures.append('harness exited nonzero: %d' % proc.returncode)
        m = re.search(r'SMMU4K_RESULT \{"iterations":(\d+),"failures":(\d+)\}', text)
        if not m:
            failures.append('harness result line missing')
        else:
            iterations = int(m.group(1))
            if int(m.group(2)):
                failures.append('harness reports %d failures' % int(m.group(2)))
        if 'ERROR' in text and 'runtime error' in text:
            failures.append('sanitizer report detected')
        if iterations < 1000:
            failures.append('iteration count below contract: %d' % iterations)
        positive, negative = 16, 8

    report = {
        'passed': not failures,
        'case_counts': {'positive': positive, 'negative': negative},
        'iterations': iterations,
        'failures': failures,
        'device_access': False,
        'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print(json.dumps(report))
    if failures:
        raise SystemExit('smmu4k suite failed: ' + '; '.join(failures))
    print('PASS: arm-smmu4k-and-sm8750-smmu suite; %d positive + %d negative, %d iterations'
          % (positive, negative, iterations))

if __name__ == '__main__':
    main()
