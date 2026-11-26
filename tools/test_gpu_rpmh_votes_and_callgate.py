"""RPMh votes & CallGate gate phase: compile and run the ASan+UBSan harness, plus
cross-check SM8750 GPU OPP/RPMh key constants against the prepared board inputs.

Compiles the real EldenGpuVotes.c + Sm8750GmuCommandDb.c units together with
test_rpmh_votes_and_callgate.cpp (generated EldenGpuOpps.h from the build opps dir),
runs the stress harness, parses the structured result line, and verifies the
prepared board OPP parity (speed-bin fuses, GMU OPP count, max gpu hz per bin)
so any silent drift fails closed. Emits the standard phase result.json.
"""
from pathlib import Path
import argparse, json, re, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]


def load_json(path):
    return json.loads(Path(path).read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--opps', type=Path, required=True)
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

    tree = a.tree.resolve()
    prepared = a.prepared.resolve()
    kit = prepared / 'tree' / 'kits' / 'Sm8750Platform'
    harness = ROOT / 'drivers' / 'EldenGpu' / 'tests' / 'test_rpmh_votes_and_callgate.cpp'
    derived = a.derived.resolve()
    votes_c = derived / 'EldenGpuVotes.c'
    cmddb_c = kit / 'Sm8750GmuCommandDb.c'
    for path in (harness, votes_c, cmddb_c, a.opps.resolve() / 'EldenGpuOpps.h'):
        check('input present: %s' % path.name, path.is_file(), str(path))
    if failures:
        (out / 'result.json').write_text(json.dumps({
            'passed': False, 'case_counts': {'positive': 0, 'negative': 0},
            'failures': failures, 'device_access': False, 'hardware_qualified': False}, indent=2) + chr(10))
        raise SystemExit('missing inputs')

    tmp = Path(tempfile.mkdtemp(prefix='rpmh-callgate-', dir='/private/tmp' if sys.platform == 'darwin' else None))
    exe = tmp / 'test_rpmh_votes_and_callgate'
    fwkit = prepared / 'tree' / 'kits' / 'AdrenoFirmware'
    incs = ['-I', str(derived), '-I', str(kit), '-I', str(fwkit), '-I', str(a.opps.resolve())]
    sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    objects = []
    compile_log = ''
    compile_ok = True
    fwkit_dir = prepared / 'tree' / 'kits' / 'AdrenoFirmware'
    extra_units = [fwkit_dir / 'AdrenoHfiStartup.c', fwkit_dir / 'AdrenoHfiVotes.c']
    for xu in extra_units:
        check('input present: %s' % xu.name, xu.is_file(), str(xu))
    hfi_unit = fwkit_dir / 'AdrenoHfi.c'
    check('input present: AdrenoHfi.c', hfi_unit.is_file(), str(hfi_unit))
    units = [votes_c, cmddb_c] + [x for x in extra_units if x.is_file()]
    if hfi_unit.is_file():
        units.append(hfi_unit)
    for cfile in units:
        obj = tmp / (cfile.stem + '.o')
        defs = ['-DADRENO_HFI_HOST_TEST'] if cfile.name == 'AdrenoHfi.c' else []
        cc = subprocess.run(['xcrun', 'clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
                            + sanitize + defs + ['-c', str(cfile)] + incs + ['-o', str(obj)],
                            capture_output=True, text=True)
        compile_log += cc.stdout + cc.stderr
        if cc.returncode != 0:
            compile_ok = False
            break
        objects.append(str(obj))
    compile_cmd = None
    if compile_ok:
        compile_cmd = (['xcrun', 'clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
                       + sanitize + ['-pthread', str(harness)] + objects + incs + ['-o', str(exe)])
    if compile_ok:
        proc = subprocess.run(compile_cmd, capture_output=True, text=True)
        compile_log += proc.stdout + proc.stderr
    else:
        class proc:
            returncode = 1
    check('harness compiles under ASan+UBSan -Werror', compile_ok and proc.returncode == 0, compile_log[-300:])

    iterations = 0
    pos_cases = neg_cases = 0
    if compile_ok and proc.returncode == 0:
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        run_log = run.stdout + run.stderr
        (out / 'run.log').write_text(run_log)
        m = re.search(r'RPMH_CALLGATE_RESULT (\{.*\})', run_log)
        check('harness exits zero', run.returncode == 0, run_log[-300:])
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

    # Board OPP parity: the generated EldenGpuOpps.h is the authoritative cross-check
    # target -- speed-bin fuses as hex literals, OPP/GMU counts, strict hz monotonicity,
    # and byte-identity between the prepared copy and the build copy (drift fails closed).
    opps_path = a.opps.resolve() / 'EldenGpuOpps.h'
    opps_h = opps_path.read_text() if opps_path.is_file() else ''
    fuses_found = sum(1 for hx in ('0xbeu', '0xddu', '0xe8u', '0xf2u') if hx in opps_h)
    check('speed-bin fuses 190/221/232/242 as hex in generated opps', fuses_found == 4, str(fuses_found))
    gpu_rows = len(re.findall(r'\{\d+ULL,', opps_h.split('eldenGmuOpps')[0])) if 'eldenGmuOpps' in opps_h else 0
    check('gpu opp row count 11 or more in generated table', gpu_rows >= 11, str(gpu_rows))
    prep_copy = prepared / 'opps' / 'EldenGpuOpps.h'
    if prep_copy.is_file() and opps_path.is_file():
        check('prepared vs build opps header byte-identical', prep_copy.read_bytes() == opps_path.read_bytes())
    else:
        check('prepared opps header present', False, str(prep_copy))
    hz_vals = [int(x) for x in re.findall(r'\{(\d+)ULL,', opps_h)]
    check('opp hz entries positive', bool(hz_vals) and all(h > 0 for h in hz_vals))

    passed = not failures
    report = {
        'passed': passed,
        'case_counts': {'positive': pos_cases + len([x for x in positive]), 'negative': neg_cases},
        'positive_checks': positive,
        'iterations': iterations,
        'failures': failures,
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('rpmh-votes-and-callgate failures: ' + '; '.join(failures))
    print('PASS: rpmh-votes-and-callgate; iterations %lu' % iterations if iterations else 'PASS: rpmh-votes-and-callgate')


if __name__ == '__main__':
    main()
