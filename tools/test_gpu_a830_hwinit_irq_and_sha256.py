"""A830 HwInit/IRQ/SHA-256/SocInfo gate phase: compile and run the ASan+UBSan
harness against the real pin kit units, emit standard phase result.json."""
from pathlib import Path
import argparse, json, re, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--opps', type=Path, required=False)
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
    kit = prepared / 'tree' / 'kits' / 'Sm8750Platform'
    harness = ROOT / 'drivers' / 'EldenGpu' / 'tests' / 'test_a830_hwinit_irq_and_sha256.cpp'
    units_cpp = [adreno / 'NativeAdrenoSha256.cpp', adreno / 'NativeAdrenoIrq.cpp']
    units_c = [kit / 'Sm8750SocInfo.c']
    for path in [harness] + units_cpp + units_c:
        check('input present: %s' % path.name, path.is_file(), str(path))
    if failures:
        (out / 'result.json').write_text(json.dumps({'passed': False, 'case_counts': {'positive': 0, 'negative': 0},
            'failures': failures, 'device_access': False, 'hardware_qualified': False}, indent=2) + chr(10))
        raise SystemExit('missing inputs')

    tmp = Path(tempfile.mkdtemp(prefix='hwinit-irq-', dir='/private/tmp' if sys.platform == 'darwin' else None))
    exe = tmp / 'test_a830_hwinit_irq_and_sha256'
    # Established shim assembly (same as kmd-stress-fences phase): empty IOKit/kern
    # stub headers; the pin's XnuMemoryFixture.h supplies the host doubles.
    stubs = out / 'host-headers'
    for rel in ('IOKit/IOTypes.h', 'IOKit/IOLib.h', 'IOKit/IOBufferMemoryDescriptor.h', 'kern/task.h'):
        f = stubs / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.' + chr(10))
    kits = prepared / 'tree' / 'kits'
    fixture = adreno / 'tests' / 'XnuMemoryFixture.h'
    check('fixture present', fixture.is_file(), str(fixture))
    includes = ['-I', str(adreno), '-I', str(adreno / 'tests'), '-I', str(stubs), '-I', str(kit),
                '-I', str(kits / 'AdrenoFirmware'), '-I', str(kits / 'AdrenoPM4')]
    flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-DNATIVE_ADRENO_HOST_TEST', '-include', str(fixture)] + includes
    sanitize = []
    compile_log = ''
    objects = []
    compile_ok = True
    proc_rc = 1
    # SocInfo C unit compiles as C++ under the same fixture flags (it is C-clean).
    objects = []
    compile_ok = True
    compile_log = ''
    for cfile in units_c:
        if not cfile.is_file():
            continue
        obj = tmp / (cfile.stem + '.o')
        cc = subprocess.run(['xcrun', 'clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                             '-c', str(cfile), '-I', str(kit), '-o', str(obj)], capture_output=True, text=True)
        compile_log += cc.stdout + cc.stderr
        if cc.returncode != 0:
            compile_ok = False
            break
        objects.append(str(obj))
    proc_rc = 1
    if compile_ok:
        sources = [str(harness)] + [str(u) for u in units_cpp if u.is_file()]
        link = subprocess.run(['xcrun', 'clang++'] + flags + ['-pthread'] + sources + objects + ['-o', str(exe)],
                              capture_output=True, text=True)
        compile_log += link.stdout + link.stderr
        proc_rc = link.returncode
        compile_ok = proc_rc == 0
    (out / 'build.log').write_text(compile_log)
    check('harness compiles under ASan+UBSan -Werror', compile_ok and proc_rc == 0, compile_log[-300:])

    iterations = 0
    pos_cases = neg_cases = 0
    if compile_ok and proc_rc == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        run_log = run.stdout + run.stderr
        (out / 'run.log').write_text(run_log)
        check('harness exits zero', run.returncode == 0, run_log[-300:])
        m = re.search(r'HWINIT_IRQ_SHA_RESULT (\{.*\})', run_log)
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
        raise SystemExit('a830-hwinit-irq-and-sha256 failures: ' + '; '.join(failures))
    print('PASS: a830-hwinit-irq-and-sha256; iterations %d' % iterations)


if __name__ == '__main__':
    main()
