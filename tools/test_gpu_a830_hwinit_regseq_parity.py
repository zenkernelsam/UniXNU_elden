"""A830 HwInit register-sequence parity gate phase: compile and run the ASan+UBSan
harness against the real pin HwInit/Ring/CpInit units, emit standard phase result.json."""
from pathlib import Path
import argparse, json, re, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    positive, negative, failures = [], [], []

    def check(name, ok, detail=''):
        if ok:
            positive.append(name)
        else:
            failures.append(name + (': ' + detail if detail else ''))

    prepared = a.prepared.resolve()
    adreno = prepared / 'tree' / 'drivers' / 'Adreno740'
    harness = ROOT / 'drivers' / 'EldenGpu' / 'tests' / 'test_a830_hwinit_regseq_parity.cpp'
    candidate_units = ['NativeAdrenoA830HwInit.cpp', 'NativeAdrenoRing.cpp', 'NativeAdrenoCpInit.cpp',
                       'NativeAdrenoPreempt.cpp', 'NativeAdrenoPwrup.cpp']
    units = [adreno / u for u in candidate_units]
    for path in [harness] + units:
        check('input present: %s' % path.name, path.is_file(), str(path))
    if failures:
        (out / 'result.json').write_text(json.dumps({'passed': False, 'case_counts': {'positive': 0, 'negative': 0},
            'failures': failures, 'device_access': False, 'hardware_qualified': False}, indent=2) + chr(10))
        raise SystemExit('missing inputs')

    tmp = Path(tempfile.mkdtemp(prefix='hwinit-regseq-', dir='/private/tmp' if sys.platform == 'darwin' else None))
    exe = tmp / 'test_a830_hwinit_regseq_parity'
    stubs = out / 'host-headers'
    for rel in ('IOKit/IOTypes.h', 'IOKit/IOLib.h', 'IOKit/IOBufferMemoryDescriptor.h', 'kern/task.h'):
        f = stubs / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.' + chr(10))
    kits = prepared / 'tree' / 'kits'
    fixture = adreno / 'tests' / 'XnuMemoryFixture.h'
    check('fixture present', fixture.is_file(), str(fixture))
    includes = ['-I', str(adreno), '-I', str(adreno / 'tests'), '-I', str(stubs),
                '-I', str(kits / 'AdrenoFirmware'), '-I', str(kits / 'AdrenoPM4'), '-I', str(kits / 'Sm8750Platform')]
    flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-DNATIVE_ADRENO_HOST_TEST', '-include', str(fixture)] + includes
    objects = []
    compile_log = ''
    pm4_c = kits / 'AdrenoPM4' / 'AdrenoPM4.c'
    if pm4_c.is_file():
        obj = tmp / 'AdrenoPM4.o'
        cc = subprocess.run(['xcrun', 'clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                             '-c', str(pm4_c), '-I', str(kits / 'AdrenoPM4'), '-o', str(obj)], capture_output=True, text=True)
        compile_log += cc.stdout + cc.stderr
        if cc.returncode == 0:
            objects.append(str(obj))
    sources = [str(harness)] + [str(u) for u in units if u.is_file()]
    link = subprocess.run(['xcrun', 'clang++'] + flags + ['-pthread'] + sources + objects + ['-o', str(exe)],
                          capture_output=True, text=True)
    compile_log += link.stdout + link.stderr
    (out / 'build.log').write_text(compile_log)
    check('harness compiles under ASan+UBSan -Werror', link.returncode == 0, compile_log[-300:])

    iterations = 0
    pos_cases = neg_cases = 0
    if link.returncode == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        run_log = run.stdout + run.stderr
        (out / 'run.log').write_text(run_log)
        check('harness exits zero', run.returncode == 0, run_log[-300:])
        m = re.search(r'HWINIT_REGSEQ_RESULT (\{.*\})', run_log)
        if m:
            summary = json.loads(m.group(1))
            check('harness reports passed', summary.get('passed') is True)
            iterations = summary.get('iterations', 0)
            pos_cases = summary.get('positive', 0)
            neg_cases = summary.get('negative', 0)
            check('stress iterations >= 1000', iterations >= 1000, str(iterations))
        else:
            check('result line present', False, run_log[-200:])
        check('no sanitizer report', 'ERROR' not in run_log and 'runtime error' not in run_log)

    passed = not failures
    report = {
        'passed': passed,
        'case_counts': {'positive': pos_cases + len(positive), 'negative': neg_cases},
        'positive_checks': positive,
        'iterations': iterations,
        'failures': failures,
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('a830-hwinit-regseq-parity failures: ' + '; '.join(failures))
    print('PASS: a830-hwinit-regseq-parity; iterations %d' % iterations)


if __name__ == '__main__':
    main()
