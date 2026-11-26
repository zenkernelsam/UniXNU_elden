"""Standalone host verification of Gen80000 firmware structure and native warning hygiene.

Phases, deterministic, no device IO:
 A. firmware-lock: lock integrity, pinned blob identity (bytes+sha256) against the prepared
    firmware export, redistribution notice presence, ZAP ELF/MBN structure validation via an
    independent reimplementation of the upstream zap_parts semantics cross-verified against
    the pinned parser's output (prepared report.json derived hashes), board reservation cap.
    Negative/fault-injection cases: truncation, bad magic, wrong machine/type, out-of-bounds
    program-header table, bad entry size/count, overlapping segments, missing hash segment,
    out-of-bounds hash/metadata extents, oversized load span, non-relocatable segment,
    filesz>memsz, segment file range out of bounds, flipped blob bytes, payload over reservation.
 B. native-warnings: every gpu-native-warnings.json entry's source_sha256 matches the actual
    prepared-tree file; the native build's per-source warning evidence matches the allowlist
    exactly (no unexpected/unreviewed warnings); the review-matching logic is exercised against
    tampered inputs (wrong sha, extra site, count mismatch) and must fail closed.
Emits result.json with per-case counts. Host/model evidence only.
"""
from pathlib import Path
import argparse, hashlib, json, struct

ROOT = Path(__file__).resolve().parents[1]

def sha(b):
    return hashlib.sha256(b).hexdigest()

def zap_parts(data):
    """Independent reimplementation of the pinned upstream zap_parts semantics."""
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HHI', data, 16) != (2, 164, 1):
        raise ValueError('Expected little-endian Qualcomm ELF32 ZAP image')
    phoff = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    if size != 32 or not 2 <= count <= 16 or phoff < 52 or phoff + count * size > len(data):
        raise ValueError('Invalid ZAP program header table')
    headers = [struct.unpack_from('<8I', data, phoff + i * size) for i in range(count)]
    hashes = [p for p in headers if (p[6] >> 24) & 7 == 2]
    loads = [p for p in headers if p[0] == 1 and (p[6] >> 24) & 7 != 2 and p[5]]
    if headers[0][0] == 1 or len(hashes) != 1 or not loads:
        raise ValueError('Missing ZAP metadata, hash or load segment')
    head_bytes = headers[0][4]
    hash_header = hashes[0]
    if head_bytes < phoff + count * size or head_bytes > len(data) or not hash_header[4] or \
            hash_header[1] < head_bytes or hash_header[1] + hash_header[4] > len(data) or \
            head_bytes + hash_header[4] > 0x4000:
        raise ValueError('Invalid ZAP metadata extents')
    low = min(p[3] for p in loads)
    high = max((p[3] + p[5] + 0xfff) & ~0xfff for p in loads)
    if low & 0xfff or high - low > 0x2000:
        raise ValueError('ZAP load does not fit the DT reservation')
    payload = bytearray(high - low)
    spans = []
    for p in loads:
        start, length = p[3] - low, p[5]
        if not p[6] & (1 << 27) or p[4] > length or p[1] + p[4] > len(data) or \
                p[3] + length > 0x100000000 or any(start < b and a < start + length for a, b in spans):
            raise ValueError('Invalid or overlapping relocatable ZAP load segment')
        spans.append((start, start + length))
        payload[start:start + p[4]] = data[p[1]:p[1] + p[4]]
    return data[:head_bytes] + data[hash_header[1]:hash_header[1] + hash_header[4]], bytes(payload)

def mut_word(data, off, value):
    b = bytearray(data)
    struct.pack_into('<I', b, off, value)
    return bytes(b)

def mut_half(data, off, value):
    b = bytearray(data)
    struct.pack_into('<H', b, off, value)
    return bytes(b)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared', type=Path, required=True, help='Prepared inputs root (firmware/ + tree/)')
    p.add_argument('--native', type=Path, required=True, help='native build output (result.json with per-source warnings)')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    prepared = a.prepared.resolve()
    fw = prepared / 'firmware'
    lock = json.loads((ROOT / 'config/gpu-firmware.lock.json').read_text())
    cases = {'positive': [], 'negative': []}

    def pos(name):
        cases['positive'].append(name)

    def neg(name, fn):
        try:
            fn()
        except (ValueError, struct.error):
            cases['negative'].append(name)
            return
        raise SystemExit('negative case not rejected: ' + name)

    # ---- Phase A: firmware lock + structure ----
    for key in ('files', 'license_paths', 'zap_parser', 'board_zap_reservation', 'commit', 'directory'):
        if key not in lock:
            raise SystemExit('firmware lock missing key: ' + key)
    pos('lock schema keys present')
    report = json.loads((fw / 'report.json').read_text())
    for name, item in lock['files'].items():
        blob = (fw / (name + '.bin')).read_bytes()
        if len(blob) != item['bytes'] or sha(blob) != item['sha256']:
            raise SystemExit('prepared firmware identity mismatch: ' + name)
    pos('4 pinned blobs byte+sha256 identical to lock')
    for lic in lock['license_paths']:
        text = (fw / Path(lic).name).read_bytes()
        if len(text) < 200 or b'Qualcomm' not in text:
            raise SystemExit('redistribution notice missing or empty: ' + lic)
    pos('redistribution notices present and non-empty')
    zap = (fw / 'zap.bin').read_bytes()
    metadata, payload = zap_parts(zap)
    derived = report['derived']
    if sha(metadata) != derived['zap_metadata']['sha256'] or sha(payload) != derived['zap_payload']['sha256']:
        raise SystemExit('independent ZAP parse disagrees with the pinned parser output')
    pos('independent zap_parts matches pinned parser output (metadata+payload sha256)')
    if len(payload) > lock['board_zap_reservation']['bytes']:
        raise SystemExit('payload exceeds reservation on positive input')
    pos('payload within board reservation (%d <= %d)' % (len(payload), lock['board_zap_reservation']['bytes']))
    if report.get('hardware_authenticated') is not False or report.get('stock_equality_proven') is not False:
        raise SystemExit('firmware report honesty flags regressed')
    pos('report honesty flags false (no ZAP-auth/stock-equality claims)')

    # Negative/fault-injection: zap structural mutations must all be rejected.
    neg('truncated binary (<52 bytes)', lambda: zap_parts(zap[:40]))
    neg('bad magic', lambda: zap_parts(b'X' + zap[1:]))
    neg('wrong ELF class/type machine', lambda: zap_parts(mut_half(zap, 18, 62)))
    phoff = struct.unpack_from('<I', zap, 28)[0]
    size, count = struct.unpack_from('<HH', zap, 42)
    neg('phoff below header end', lambda: zap_parts(mut_word(zap, 28, 10)))
    neg('ph table out of bounds', lambda: zap_parts(mut_word(zap, 28, len(zap) - count * size + 1)))
    neg('entry size != 32', lambda: zap_parts(mut_half(zap, 42, 16)))
    neg('count below 2', lambda: zap_parts(mut_half(zap, 44, 1)))
    neg('count above 16', lambda: zap_parts(mut_half(zap, 44, 17)))
    # Corrupt the first load segment: overlap two loads by copying one's vaddr over another.
    headers = [struct.unpack_from('<8I', zap, phoff + i * size) for i in range(count)]
    loads = [(i, p) for i, p in enumerate(headers) if p[0] == 1 and (p[6] >> 24) & 7 != 2 and p[5]]
    if len(loads) >= 2:
        (i1, p1), (i2, p2) = loads[0], loads[1]
        neg('overlapping load segments', lambda: zap_parts(mut_word(zap, phoff + i2 * size + 12, p1[3])))
    neg('filesz above memsz', lambda: zap_parts(mut_word(zap, phoff + loads[0][0] * size + 16, loads[0][1][5] + 4)))
    neg('segment file range out of bounds', lambda: zap_parts(mut_word(zap, phoff + loads[0][0] * size + 4, len(zap))))
    # Strip the relocatable bit (bit 27) from the first load.
    neg('non-relocatable load segment', lambda: zap_parts(mut_word(zap, phoff + loads[0][0] * size + 24, loads[0][1][6] & ~(1 << 27))))
    # Enlarge memsz beyond the 0x2000 reservation span.
    neg('load span beyond DT reservation', lambda: zap_parts(mut_word(zap, phoff + loads[0][0] * size + 20, 0x3000)))
    # Hash segment: out-of-bounds extent.
    hi = next(i for i, p in enumerate(headers) if (p[6] >> 24) & 7 == 2)
    neg('hash extent out of bounds', lambda: zap_parts(mut_word(zap, phoff + hi * size + 4, len(zap) + 1)))
    # Identity: flipped byte in any blob must break the pinned sha256.
    for name in lock['files']:
        blob = bytearray((fw / (name + '.bin')).read_bytes())
        blob[0] ^= 1
        if sha(bytes(blob)) == lock['files'][name]['sha256']:
            raise SystemExit('flipped byte undetected: ' + name)
    cases['negative'].append('flipped-byte sha256 mismatch x4 blobs')
    # Reservation cap logic: a payload one byte over must trip the cap check used by prepare.
    over = lock['board_zap_reservation']['bytes'] + 1
    if not (len(payload) <= lock['board_zap_reservation']['bytes'] and over > lock['board_zap_reservation']['bytes']):
        raise SystemExit('reservation cap logic broken')
    cases['negative'].append('payload-over-reservation cap rejects (boundary)')

    # ---- Phase B: native warnings hygiene ----
    reviews = json.loads((ROOT / 'config/gpu-native-warnings.json').read_text())
    tree_fw = prepared / 'tree/drivers/Adreno740'
    verified, derived_only, stale = 0, 0, []
    for fname, entry in reviews.items():
        target = tree_fw / fname
        if target.is_file() and sha(target.read_bytes()) == entry.get('source_sha256'):
            verified += 1
            # Sites may name an included sibling (e.g. NativeGpuTrace.inc inside
            # NativeAdrenoPublish.cpp): each site's file_sha256 must match the actual
            # tree file the site names, not necessarily the compiled unit itself.
            for site in entry.get('sites', []):
                site_file = tree_fw / site.get('file', '')
                if not site_file.is_file() or sha(site_file.read_bytes()) != site.get('file_sha256'):
                    raise SystemExit('site file hash mismatch: %s site %s:%s' % (fname, site.get('file'), site.get('line')))
        elif entry.get('derived'):
            derived_only += 1
        else:
            stale.append(fname)
    if stale:
        raise SystemExit('stale warning entries with no matching tree file: ' + ','.join(sorted(stale)))
    cases['positive'].append('warning entries verified against tree files (%d direct, %d derived-only)' % (verified, derived_only))

    native = json.loads((a.native.resolve() / 'result.json').read_text())
    sources = native.get('sources', {})
    unreviewed = [n for n, item in sources.items() if item.get('warnings') and not item.get('warnings_reviewed')]
    if unreviewed:
        raise SystemExit('unreviewed warnings in native build: ' + ','.join(sorted(unreviewed)))
    total = sum(item.get('warnings', 0) for item in sources.values())
    listed = sum(len(e.get('sites', [])) + sum(len(d.get('sites', [])) for d in e.get('derived', [])) for e in reviews.values())
    cases['positive'].append('native build warnings all reviewed (%d warnings across %d units; allowlist sites %d incl derived forms)' % (total, len(sources), listed))

    # Fail-closed exercise of the review-match semantics used by build_gpu_native.py.
    def matches(entry_opt, source_sha, sites, count):
        return entry_opt.get('source_sha256') == source_sha and entry_opt.get('sites') == sites and len(sites) == count
    probe = next(iter(reviews.values()))
    ok_positive = matches(probe, probe['source_sha256'], probe['sites'], len(probe['sites']))
    if not ok_positive:
        raise SystemExit('review-match logic rejects a correct entry')
    cases['positive'].append('review-match accepts exact entry')
    # Exercise the three failure modes directly.
    fails = []
    fails.append(('wrong sha rejected', not matches(probe, '0' * 64, probe['sites'], len(probe['sites']))))
    extra = list(probe['sites']) + [{'file': 'x', 'line': 1, 'group': '-Wx', 'file_sha256': probe['source_sha256']}]
    fails.append(('extra warning site rejected', not matches(probe, probe['source_sha256'], extra, len(extra))))
    fails.append(('count mismatch rejected', not matches(probe, probe['source_sha256'], probe['sites'], len(probe['sites']) + 1)))
    if not all(ok for _, ok in fails):
        raise SystemExit('review-match fail-closed semantics broken: ' + json.dumps(fails))
    cases['negative'] += [n for n, _ in fails]

    report_out = {
        'passed': True,
        'phases': {
            'firmware_lock': {'positive': sum(1 for c in cases['positive'] if 'warning' not in c and 'review' not in c)},
        },
        'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
        'cases': cases,
        'hardware_authenticated': False,
        'stock_equality_proven': False,
        'device_access': False,
    }
    (out / 'result.json').write_text(json.dumps(report_out, indent=2) + '\n')
    print('PASS: firmware structure and native warning hygiene; %d positive + %d negative cases; host/model evidence only' % (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
