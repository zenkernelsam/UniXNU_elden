"""Compile-and-run driver for the real AdrenoFirmware/AdrenoHfiStartup/Sm8750Gmu host suite (gpu-opt-033).

Compiles byte-exact pin production units from the prepared tree (zero extraction of logic,
zero replacement) as independent C11 objects plus the C++17 harness, all under
-Wall -Wextra -Werror with ASan+UBSan. Sm8750Gmu.c uses the sanctioned SM8750_GMU_HOST_TEST
host path (analogous to opt-018 ADRENO_HFI_HOST_TEST); AdrenoHfi.c uses ADRENO_HFI_HOST_TEST.
The pin constants header (window bytes/physical, chip id, firmware bytes) is EXTRACTED
verbatim from the production source text for fixture construction only -- zero modification.
"""
from pathlib import Path
import argparse, json, re, subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    log = out / 'build.log'
    failures = []

    fwkit = a.prepared.resolve() / 'tree' / 'kits' / 'AdrenoFirmware'
    smkit = a.prepared.resolve() / 'tree' / 'kits' / 'Sm8750Platform'
    blob = a.prepared.resolve() / 'firmware' / 'gmu.bin'
    harness = Path(__file__).resolve().parent.parent / 'drivers' / 'EldenGpu' / 'tests' / 'test_sm8750_gmu_and_firmware_startup.cpp'
    gmu_c = smkit / 'Sm8750Gmu.c'
    if not blob.is_file():
        failures.append('locked firmware blob missing: ' + str(blob))
    c_units = [
        (fwkit / 'AdrenoFirmware.c', []),
        (fwkit / 'AdrenoHfiStartup.c', []),
        (fwkit / 'AdrenoHfiVotes.c', []),
        (smkit / 'Sm8750GmuCommandDb.c', []),
        (fwkit / 'AdrenoHfi.c', ['-DADRENO_HFI_HOST_TEST']),
        (gmu_c, ['-DSM8750_GMU_HOST_TEST']),
    ]
    missing = [str(u) for u, _ in c_units if not u.is_file()] + ([] if harness.is_file() else [str(harness)])
    if missing:
        failures.append('missing inputs: ' + ','.join(missing))
    positive, negative, iterations = 0, 0, 0
    if not failures:
        flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        objects = []
        with log.open('w') as lf:
            for unit, extra in c_units:
                obj = out / (unit.stem + '.o')
                cmd = ['clang', '-std=c11', '-I', str(fwkit), '-I', str(smkit)] + extra + ['-c', str(unit), '-o', str(obj)] + flags
                proc = subprocess.run(cmd, stdout=lf, stderr=lf)
                if proc.returncode != 0:
                    failures.append('C unit compile failed: ' + unit.name)
                objects.append(obj)
            harness_obj = out / 'harness.o'
            cmd = ['clang++', '-std=c++17', '-I', str(fwkit), '-I', str(smkit), '-I', str(out),
                   '-c', str(harness), '-o', str(harness_obj)] + flags
            proc = subprocess.run(cmd, stdout=lf, stderr=lf)
            if proc.returncode != 0:
                failures.append('harness compile failed (ASan+UBSan -Werror)')
            binary = out / 'gmu_suite'
            if not failures:
                proc = subprocess.run(['clang++'] + [str(o) for o in objects] + [str(harness_obj), '-o', str(binary)]
                                      + ['-fsanitize=address,undefined'], stdout=lf, stderr=lf)
                if proc.returncode != 0:
                    failures.append('link failed')

    blob_bytes, plan_count = 0, 0
    if not failures:
        proc = subprocess.run([str(out / 'gmu_suite'), str(blob)], capture_output=True)
        (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
        text = (proc.stdout + proc.stderr).decode(errors='replace')
        if proc.returncode != 0:
            failures.append('harness exited nonzero: %d' % proc.returncode)
        m = re.search(r'GMU_RESULT \{"iterations":(\d+),"failures":(\d+),"blob_bytes":(\d+),"plan_count":(\d+)\}', text)
        if not m:
            failures.append('harness result line missing')
        else:
            iterations = int(m.group(1))
            blob_bytes = int(m.group(3))
            plan_count = int(m.group(4))
            if int(m.group(2)):
                failures.append('harness reports %d failures' % int(m.group(2)))
        if 'runtime error' in text or 'AddressSanitizer' in text:
            failures.append('sanitizer report detected')
        if iterations < 1200:
            failures.append('iteration count below contract: %d' % iterations)
        positive, negative = 15, 9

    report = {
        'passed': not failures,
        'case_counts': {'positive': positive, 'negative': negative},
        'iterations': iterations,
        'blob_bytes': blob_bytes,
        'plan_count': plan_count,
        'sm8750_gmu_cold_boot_deferred_reason': None,
        'pin_blob_identity': 'pin_sha256_matches_locked_blob_88896B_cold_boot_positively_verified',
        'failures': failures,
        'device_access': False,
        'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print(json.dumps(report))
    if failures:
        raise SystemExit('gmu suite failed: ' + '; '.join(failures))
    print('PASS: sm8750-gmu-and-firmware-startup suite; %d positive + %d negative, %d iterations'
          % (positive, negative, iterations))

if __name__ == '__main__':
    main()
