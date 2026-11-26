"""Cross-layer pre-bringup contract verifier (KMD <-> userspace <-> board <-> firmware).

Deterministic host-only verification that the layers which must agree before any bringup
actually do: firmware lock vs prepared inputs vs prepared.json hash chain, board ZAP
reservation bound, parser identity, Turnip chip-id patch vs the KMD chip id, sysmem,nocb
profile, packaged Mach-O set, SP_CORES anchor, and a final rollup aggregating every prior
verification phase's result.json. Negative/fault-injection cases feed mutated copies of
the real evidence into the same pure evaluation functions, proving each check is
fail-closed. Emits result.json with matrix_totals. No device access.
"""
from pathlib import Path
import argparse, hashlib, json, tarfile
ROOT = Path(__file__).resolve().parents[1]


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text())


def zap_chain_failures(lock, prepared_json, zap_bytes, report):
    """Cross-layer firmware hash chain: lock <-> prepared.json <-> zap.bin bytes <-> report."""
    f = []
    zap = lock['files']['zap']
    if zap.get('path') != 'qcom/sm8750/gen80000_zap.mbn':
        f.append('zap lock path drift')
    if zap.get('bytes') != len(zap_bytes):
        f.append('zap bytes mismatch lock')
    if sha256_bytes(zap_bytes) != zap.get('sha256'):
        f.append('zap bytes sha256 mismatch lock')
    pj_entry = prepared_json.get('files', {}).get('firmware/zap.bin')
    if pj_entry != zap.get('sha256'):
        f.append('prepared.json zap.bin hash mismatch lock')
    resv = report.get('board_zap_reservation')
    resv_bytes = resv.get('bytes') if isinstance(resv, dict) else None
    if not isinstance(resv_bytes, int) or resv_bytes <= 0 or resv_bytes > 8192:
        f.append('board zap reservation out of bounds: %r' % (resv,))
    if report.get('parser_sha256') != lock.get('zap_parser', {}).get('sha256'):
        f.append('parser identity mismatch lock')
    return f


def macho_set_missing(base_names, required):
    return [r for r in required if r not in base_names]


def tu_debug_failures(blob):
    f = []
    if b'sysmem,nocb' not in blob:
        f.append('sysmem,nocb missing')
    if b'gmem' in blob:
        f.append('gmem present')
    return f


def rollup_failures(records):
    """records: list of (phase, present, truthy_pass). Returns failure strings."""
    f = []
    missing = [n for n, present, _ in records if not present]
    failed = [n for n, present, ok in records if present and not ok]
    if missing:
        f.append('missing phases: ' + ','.join(missing))
    if failed:
        f.append('failed phases: ' + ','.join(failed))
    if len([r for r in records if r[1]]) < 12:
        f.append('fewer than 12 prior phases present')
    return f


def pass_marker(result):
    """Truthful pass marker: suite-style `passed` or chain-style `pass`."""
    return bool(result.get('passed')) or bool(result.get('pass'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    config = ROOT / 'config'
    positive, negative, failures = [], [], []

    def check(name, ok, detail=''):
        if ok:
            positive.append(name)
        else:
            failures.append(name + (': ' + detail if detail else ''))

    def expect_reject(name, fn):
        try:
            bad = fn()
        except Exception:
            bad = []
        if bad:
            negative.append(name)
        else:
            failures.append('negative case escaped: ' + name)

    # ---- 1. Board<->Firmware<->KMD: ZAP lock/prepared/report cross-layer chain
    fwlock = load_json(config / 'gpu-firmware.lock.json')
    prepared = a.prepared.resolve()
    prepared_json = load_json(prepared / 'prepared.json')
    zap_bytes = (prepared / 'firmware/zap.bin').read_bytes()
    report = load_json(prepared / 'firmware/report.json')
    zf = zap_chain_failures(fwlock, prepared_json, zap_bytes, report)
    check('zap cross-layer hash chain + reservation + parser identity', not zf, '; '.join(zf))
    expect_reject('zap reservation > 8192 rejected',
                  lambda: zap_chain_failures(fwlock, prepared_json, zap_bytes,
                                             dict(report, board_zap_reservation=dict(
                                                 report['board_zap_reservation'], bytes=8193))))
    expect_reject('zap byte flip rejected',
                  lambda: zap_chain_failures(fwlock, prepared_json,
                                             bytes([zap_bytes[0] ^ 1]) + zap_bytes[1:], report))
    expect_reject('parser identity drift rejected',
                  lambda: zap_chain_failures(fwlock, prepared_json, zap_bytes,
                                             dict(report, parser_sha256='00' * 32)))

    # ---- 2. SP_CORES anchor in the build tool
    metal_builder = (ROOT / 'tools' / 'build_gpu_metal.py').read_text()

    def sp_cores_failures(text):
        if 'NATIVE_ADRENO_SP_CORES=6' not in text or 'NATIVE_ADRENO_SP_CORES=4' in text                 or 'NATIVE_ADRENO_SP_CORES=8' in text:
            return ['SP_CORES anchor violated']
        return []
    check('NATIVE_ADRENO_SP_CORES=6 anchor', not sp_cores_failures(metal_builder))
    expect_reject('SP_CORES mutation to 8 rejected',
                  lambda: sp_cores_failures(metal_builder.replace(
                      'NATIVE_ADRENO_SP_CORES=6', 'NATIVE_ADRENO_SP_CORES=8')))

    # ---- 3. Turnip<->KMD: chip-id patch covers the KMD bringup chip id 0x44050001
    patch_text = (config / 'inputs/mesa-patches/freedreno-a830-chip-id-variants.patch').read_text()

    def chip_failures(text):
        f = []
        for cid in ('0x44050001', '0xffff44050000', '0xffff44050001'):
            if cid not in text:
                f.append('chip id %s missing' % cid)
        return f
    check('chip-id patch covers KMD 0x44050001 + wildcards', not chip_failures(patch_text),
          '; '.join(chip_failures(patch_text)))
    expect_reject('chip-id 0x44050001 removal rejected',
                  lambda: chip_failures(patch_text.replace('0x44050001', '0x66000001')))

    # ---- 4/5. Package: tu_debug sysmem,nocb + exact Mach-O binary set
    pkg = a.package.resolve()
    tar_path = pkg if pkg.is_file() else (sorted(pkg.glob('*.tar.gz')) or sorted(pkg.glob('*.tgz')))[0]
    members, names = {}, []
    with tarfile.open(tar_path) as tf:
        for m in tf.getmembers():
            names.append(m.name)
            if m.isfile() and m.size < 1 << 20:
                members[m.name] = tf.extractfile(m).read()
    tu_debug = next((d for n, d in members.items() if 'tu_debug' in n), b'')
    tf_fail = tu_debug_failures(tu_debug)
    check('tu_debug sysmem,nocb exact, no gmem', not tf_fail, '; '.join(tf_fail))
    expect_reject('gmem flip rejected',
                  lambda: tu_debug_failures(tu_debug.replace(b'sysmem,nocb', b'sysmem,gmem')))
    required = ['kernel.macho', 'native.macho', 'NativeAdrenoMetal', 'libvulkan_freedreno.dylib', 'adrenoprobed']
    base_names = [n.split('/')[-1] for n in names]
    missing = macho_set_missing(base_names, required)
    check('packaged Mach-O set complete', not missing, 'missing: ' + ','.join(missing))
    expect_reject('missing Mach-O member rejected',
                  lambda: macho_set_missing([b for b in base_names if b != 'adrenoprobed'], required))

    # ---- 6. Unified gate rollup over prior phase result.json files
    phase_dirs = sorted(d for d in out.parent.iterdir() if d.is_dir() and d.name != out.name)
    records, rollup = [], {}
    total_pos = total_neg = total_iter = 0
    for d in phase_dirs:
        rj = d / 'result.json'
        if not rj.is_file():
            records.append((d.name, False, False))
            continue
        try:
            r = load_json(rj)
        except Exception:
            records.append((d.name, True, False))
            continue
        ok = pass_marker(r)
        records.append((d.name, True, ok))
        counts = r.get('case_counts', {})
        rollup[d.name] = {'passed': ok, 'positive': counts.get('positive', 0),
                          'negative': counts.get('negative', 0),
                          'iterations': r.get('iterations', 0) if isinstance(r.get('iterations'), int) else 0}
        total_pos += counts.get('positive', 0)
        total_neg += counts.get('negative', 0)
        total_iter += rollup[d.name]['iterations']
    rf = rollup_failures(records)
    check('rollup: all prior phases present+passed', not rf, '; '.join(rf))
    expect_reject('rollup rejects failed prior phase',
                  lambda: rollup_failures([(n, pr, False if i == 0 else ok)
                                           for i, (n, pr, ok) in enumerate(records)]))
    expect_reject('rollup rejects missing prior phase',
                  lambda: rollup_failures([(n, False if i == 0 else pr, ok)
                                           for i, (n, pr, ok) in enumerate(records)]))

    passed = not failures
    report_out = {
        'passed': passed,
        'case_counts': {'positive': len(positive), 'negative': len(negative)},
        'positive': positive, 'negative': negative, 'failures': failures,
        'matrix_totals': {
            'phases': len(rollup), 'positive': total_pos, 'negative': total_neg,
            'iterations': total_iter, 'per_phase': rollup,
        },
        'device_access': False, 'hardware': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report_out, indent=2) + chr(10))
    if not passed:
        raise SystemExit('pre-bringup contract failures: ' + '; '.join(failures))
    print('PASS: pre-bringup contract; %d positive + %d negative; rollup %d phases %d+%d cases' %
          (len(positive), len(negative), len(rollup), total_pos, total_neg))


if __name__ == '__main__':
    main()
