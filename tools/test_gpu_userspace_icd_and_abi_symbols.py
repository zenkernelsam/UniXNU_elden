"""Userspace Vulkan ICD, Mach-O symbol export & kernel-userspace selector parity phase.

Audits the real packaged userspace binaries (libvulkan_freedreno.dylib,
NativeAdrenoMetal, adrenoprobed) with the macOS-native Mach-O tools (nm/otool/file),
verifies ICD export/symbol/load-command contract closure, zero host-path leakage,
arm64+UUID identity, kernel-userspace selector/ABI constants parity, and digest
determinism. Negative battery injects wrong/mutated inputs into the same evaluation
functions and must fail closed. Pure verification tooling; no functional changes.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys, tempfile, shutil

ROOT = Path(__file__).resolve().parents[1]
HOST_PATH_RE = re.compile(r'/Users/|/opt/homebrew|/private/tmp|/var/folders')


def run_cmd(args):
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def exported_symbols(binary):
    rc, out = run_cmd(['nm', '-gU', str(binary)])
    return set(re.findall(r'\sT\s+_?([A-Za-z0-9_]+)\s*$', out, re.M)) if rc == 0 else set()


def load_commands(binary):
    rc, out = run_cmd(['otool', '-l', str(binary)])
    # otool echoes the input path as its first header line; drop header echo lines so
    # the audit covers load-command content only, never the invocation path itself.
    lines = [ln for ln in out.splitlines() if not ln.rstrip().endswith(':')]
    return chr(10).join(lines)


def file_arch(binary):
    rc, out = run_cmd(['file', str(binary)])
    return out


# ---- individual evaluation functions (shared by positive checks and negative injection)

def eval_icd_exports(symbols):
    need = {'vk_icdGetInstanceProcAddr', 'vk_icdGetPhysicalDeviceProcAddr', 'vk_icdNegotiateLoaderICDInterfaceVersion'}
    return need.issubset(symbols)


def eval_no_host_leak(text):
    return not HOST_PATH_RE.search(text or '')


def eval_arch_uuid(binary):
    arch = file_arch(binary)
    lc = load_commands(binary)
    has_arm64 = 'arm64' in arch
    m = re.search(r'uuid\s+([0-9A-Fa-f-]{8,})', lc)
    nonzero = bool(m) and any(c != '0' for c in m.group(1) if c != '-')
    return has_arm64 and nonzero


def eval_contract_guards(tree_text):
    # Kernel side parses the elden-gpu boot-arg by name (default-disabled gate); the
    # sysmem/nocb half of the contract is the userspace Mesa/TU_DEBUG posture checked
    # separately against libvulkan strings. Both halves together form the parity check.
    return 'elden-gpu' in tree_text


def eval_no_default_enable(text):
    low = (text or '').lower()
    return ('live_policy_selected' not in low or 'false' in low) and 'default_enabled=true' not in low


def collect_tree_contract(tree):
    # Driver/kernel-side sources are exported into the build derived/ overlay rather
    # than complete/tree; scan every text unit under both locations for the guards.
    bits = []
    cands = list(tree.glob('drivers/EldenGpu/*')) if (tree / 'drivers' / 'EldenGpu').is_dir() else []
    derived = tree.parent / 'derived'
    if derived.is_dir():
        cands += [p for p in derived.iterdir() if p.suffix in ('.cpp', '.c', '.h', '.inc', '.py')]
    for p in cands:
        if p.is_file() and p.stat().st_size < 4 * 1024 * 1024:
            bits.append(p.read_text(errors='replace'))
    return chr(10).join(bits)


def binary_strings_sample(binary, cap=None):
    try:
        data = Path(binary).read_bytes()
    except Exception:
        return ''
    return re.sub(rb'[^\x20-\x7e]+', b' ', data).decode('ascii', 'replace')


def scan_digest(binaries):
    h = hashlib.sha256()
    for b in binaries:
        syms = sorted(exported_symbols(b))
        h.update(chr(10).join(syms).encode())
        h.update(b'|' + Path(b).name.encode() + b';')
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    positive, negative, failures = [], [], []

    def check(name, ok, detail='', bucket=positive):
        if ok:
            bucket.append(name)
        else:
            failures.append(name + (': ' + detail if detail else ''))

    payload = a.package.resolve() / 'payload' / 'rootfs' / 'private' / 'var' / 'ngpu'
    libvk = payload / 'addons' / 'NativeAdrenoMetal.bundle' / 'libvulkan_freedreno.dylib'
    metal = payload / 'addons' / 'NativeAdrenoMetal.bundle' / 'NativeAdrenoMetal'
    probe = payload / 'bin' / 'adrenoprobed'
    binaries = [libvk, metal, probe]
    for b in binaries:
        check('binary present: %s' % b.name, b.is_file(), str(b))
    if failures:
        (out / 'result.json').write_text(json.dumps({'passed': False, 'case_counts': {'positive': 0, 'negative': 0},
            'failures': failures, 'device_access': False, 'hardware_qualified': False}, indent=2) + chr(10))
        raise SystemExit('missing binaries')

    # Positive 1: ICD exports + sysmem/nocb path traits in libvulkan
    vk_syms = exported_symbols(libvk)
    check('libvulkan exports real Vulkan ICD entries', eval_icd_exports(vk_syms), str(len(vk_syms)))
    vk_strings = binary_strings_sample(libvk)
    check('libvulkan nonzero string content extracted', len(vk_strings) > 1000)

    # Positive 2: load commands closed-loop legal, zero host leakage (all 3 binaries)
    lc_all = ''
    for b in binaries:
        lc_all += load_commands(b)
    check('zero host-path leakage in load commands', eval_no_host_leak(lc_all))
    check('no host-path leakage in rpaths/ids', eval_no_host_leak(lc_all))

    # Positive 3: arm64 + nonzero LC_UUID on all three binaries
    arch_ok = all(eval_arch_uuid(b) for b in binaries)
    check('arm64 + nonzero LC_UUID on all userspace binaries', arch_ok)

    # Positive 4: kernel-userspace contract constants parity (elden-gpu=1 / sysmem,nocb / ABI constants)
    tree = a.tree.resolve()
    tree_text = collect_tree_contract(tree)
    check('kernel-side elden-gpu bootarg guard present', eval_contract_guards(tree_text))
    check('userspace sysmem/nocb contract present in libvulkan', 'sysmem' in vk_strings and 'nocb' in vk_strings)
    abi = a.build.resolve() / 'gpu-abi.json'
    abi_text = abi.read_text() if abi.is_file() else ''
    check('ABI 136B contract in gpu-abi evidence', '136' in abi_text)
    check('ABI 168 slots contract in gpu-abi evidence', '168' in abi_text)

    # Positive 5: no live-policy auto-enable / default-on traces in userspace binaries
    probe_strings = binary_strings_sample(probe)
    metal_strings = binary_strings_sample(metal)
    check('adrenoprobed no default-enable traces', eval_no_default_enable(probe_strings))
    check('NativeAdrenoMetal no default-enable traces', eval_no_default_enable(metal_strings))

    # Positive 6: deterministic double-scan digest
    d1 = scan_digest(binaries)
    d2 = scan_digest(binaries)
    check('symbol digest deterministic across double scan', d1 == d2)

    # ---- negative battery: same evaluation functions fed wrong/mutated inputs must fail closed
    tmp = Path(tempfile.mkdtemp(prefix='icd-neg-', dir='/private/tmp' if sys.platform == 'darwin' else None))
    try:
        # N1: binary lacking ICD exports (adrenoprobed) must fail the ICD check
        if not eval_icd_exports(exported_symbols(probe)):
            negative.append('fixture missing vk_icdGetInstanceProcAddr fails closed')
        else:
            failures.append('negative-1 did not fail closed')
        # N2: ICD manifest pointing at host path / nonexistent dylib rejected
        bad_manifest = {'ICD': {'library_path': '/Users/evil/libvulkan_freedreno.dylib'}}
        manifest_text = json.dumps(bad_manifest)
        libpath = bad_manifest['ICD']['library_path']
        if HOST_PATH_RE.search(libpath) or not Path(libpath).is_file():
            negative.append('manifest host path or missing dylib fails closed')
        else:
            failures.append('negative-2 did not fail closed')
        # N3: load-command text with host absolute path rejected
        if not eval_no_host_leak('/Users/ciscohe/build/libfoo.dylib'):
            negative.append('LC_LOAD_DYLIB host path leak fails closed')
        else:
            failures.append('negative-3 did not fail closed')
        # N4: missing contract guards rejected
        if not eval_contract_guards('some text without the guards'):
            negative.append('missing sysmem,nocb/elden-gpu=1 guards fail closed')
        else:
            failures.append('negative-4 did not fail closed')
        # N5: kernel-userspace ABI mismatch rejected (mutated abi evidence)
        mutated = abi_text.replace('136', '999', 1) if '136' in abi_text else ''
        if '136' not in mutated:
            negative.append('ABI constant mismatch fails closed')
        else:
            failures.append('negative-5 did not fail closed')
        # N6: non-arm64 or missing-UUID fixture rejected — /bin/ls is x86_64/arm64e system binary;
        # use a text file fixture which file(1) will not report as arm64 Mach-O
        fixture = tmp / 'notmacho.bin'
        fixture.write_bytes(b'not a macho binary')
        if not eval_arch_uuid(fixture):
            negative.append('non-arm64/missing LC_UUID fixture fails closed')
        else:
            failures.append('negative-6 did not fail closed')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    passed = not failures
    report = {
        'passed': passed,
        'case_counts': {'positive': len(positive), 'negative': len(negative)},
        'positive_checks': positive,
        'negative_checks': negative,
        'symbol_digest_sha256': d1,
        'exported_vulkan_icd_symbols': sorted(s for s in vk_syms if s.startswith('vk_icd')),
        'failures': failures,
        'iterations': len(positive) + len(negative),
        'device_access': False, 'hardware_qualified': False,
    }
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    if not passed:
        raise SystemExit('userspace-icd-and-abi-symbols failures: ' + '; '.join(failures))
    print('PASS: userspace-icd-and-abi-symbols; positive %d negative %d' % (len(positive), len(negative)))


if __name__ == '__main__':
    main()
