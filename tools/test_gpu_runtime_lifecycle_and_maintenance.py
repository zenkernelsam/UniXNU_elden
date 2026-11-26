"""Runtime lifecycle & maintenance gate phase: extract the real derived runtime entry
bodies and the real PublisherQuiesce.inc maintenance body, compile the ASan+UBSan
harness end-to-end, run it, and verify the Runtime.h state contract consistency.

Reuses the exact extraction method of test_gpu_runtime_control.py (method() body
extraction from the derived EldenGpuRuntime.cpp, zero replacement implementations).
Emits the standard phase result.json for the pre-bringup-contract rollup.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
from test_gpu_frame import method

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--derived', type=Path, required=True)
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

    derived = a.derived.resolve()
    runtime_cpp = derived / 'EldenGpuRuntime.cpp'
    quiesce_inc_src = derived / 'PublisherQuiesce.inc'
    harness = ROOT / 'drivers' / 'EldenGpu' / 'tests' / 'test_runtime_lifecycle_and_maintenance.cpp'
    for path in (runtime_cpp, quiesce_inc_src, harness, derived / 'Runtime.h', derived / 'CallGate.h'):
        check('input present: %s' % path.name, path.is_file(), str(path))
    if failures:
        (out / 'result.json').write_text(json.dumps({
            'passed': False, 'case_counts': {'positive': 0, 'negative': 0},
            'failures': failures, 'device_access': False, 'hardware_qualified': False}, indent=2) + chr(10))
        raise SystemExit('missing inputs')

    # Extract the real entry bodies exactly like test_gpu_runtime_control.py.
    source = runtime_cpp.read_text()
    names = ['extern "C" int elden_gpu_start(',
             'extern "C" int elden_gpu_quiesce_for_reboot(',
             'extern "C" void elden_gpu_runtime_status(']
    text = chr(10).join(method(source, n) for n in names)
    (out / 'ActualRuntimeEntries.inc').write_text(text + chr(10))
    # Copy the real quiesce body into the harness include path.
    quiesce_body = quiesce_inc_src.read_text()
    (out / 'PublisherQuiesce.inc').write_text(quiesce_body)
    check('runtime entries extracted nonempty', len(text) > 200, str(len(text)))
    check('quiesce body nonempty', len(quiesce_body) > 100, str(len(quiesce_body)))

    # Runtime.h state contract consistency: enum order and struct fields must match
    # the derived tree header exactly (drift fails closed).
    runtime_h = (derived / 'Runtime.h').read_text()
    tree_h = (ROOT / 'drivers' / 'EldenGpu' / 'Runtime.h').read_text()
    enum_re = r'EldenGpuIdle,EldenGpuStarting,EldenGpuReady,EldenGpuClosing,EldenGpuDrained,EldenGpuHeld'
    check('derived runtime enum contract exact', re.search(enum_re, runtime_h) is not None)
    check('tree runtime enum contract exact', re.search(enum_re, tree_h) is not None)
    check('derived vs tree runtime header byte-identical', runtime_h == tree_h)

    exe = out / 'runtime-lifecycle-test'
    cmd = ['xcrun', 'clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread',
           '-I', str(out), '-I', str(derived), str(harness), '-o', str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out / 'build.log').write_text(r.stdout + r.stderr)
    check('harness compiles under ASan+UBSan -Werror', r.returncode == 0, (r.stdout + r.stderr)[-300:])

    iterations = 0
    pos_cases = neg_cases = 0
    if r.returncode == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        run_log = run.stdout + run.stderr
        (out / 'run.log').write_text(run_log)
        check('harness exits zero', run.returncode == 0, run_log[-300:])
        m = re.search(r'RUNTIME_LIFECYCLE_RESULT (\{.*\})', run_log)
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
        'actual_body_sha256': hashlib.sha256(text.encode()).hexdigest(),
        'failures': failures,
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('runtime-lifecycle-and-maintenance failures: ' + '; '.join(failures))
    print('PASS: runtime-lifecycle-and-maintenance; iterations %d' % iterations)


if __name__ == '__main__':
    main()
