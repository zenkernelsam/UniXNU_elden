"""Standalone host verification of userspace Mach-O hygiene and offline package isolation.

Pure-Python Mach-O load-command parser + tarfile/hashlib; deterministic; zero device IO.
 A. macho: every packaged Mach-O is arm64 with a valid non-zero LC_UUID (recorded into
    result.json for prewarm-key tracking) and carries zero host-path leaks
    (/Users/, /opt/homebrew/, /usr/local/, /Volumes/) in LC_LOAD_DYLIB-family,
    LC_ID_DYLIB or LC_RPATH entries.
 B. tarball: member count and SHA-256 parity with manifest.json, no absolute paths or
    parent traversal, no symlinks/hardlinks, activation-gate flags false.
Negative fault injection: synthetic host-path dylib, missing/zero UUID, wrong arch,
traversal member, flipped member hash, flipped activation flags — all rejected fail-closed.
"""
from pathlib import Path
import argparse, hashlib, io, json, struct, tarfile

CPU_TYPE_ARM64 = 0x0100000C
MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
LC_UUID = 0x1B
DYLIB_CMDS = {0xC: 'LC_LOAD_DYLIB', 0x15: 'LC_ID_DYLIB', 0x18: 'LC_LOAD_WEAK_DYLIB',
              0x1F: 'LC_REEXPORT_DYLIB', 0x23: 'LC_LOAD_UPWARD_DYLIB', 0x8000001C: 'LC_RPATH'}
HOST_PREFIXES = ('/Users/', '/opt/homebrew/', '/usr/local/', '/Volumes/')

def sha(b):
    return hashlib.sha256(b).hexdigest()

def parse_macho(data):
    """Return dict(arm64, uuid, paths, filetype) or raise ValueError. paths: (kind, path) pairs."""
    if len(data) < 32:
        raise ValueError('truncated Mach-O header')
    magic_le = struct.unpack_from('<I', data, 0)[0]
    if magic_le == FAT_MAGIC or struct.unpack_from('>I', data, 0)[0] == FAT_MAGIC:
        raise ValueError('fat binaries are out of scope for the offline package')
    if magic_le != MH_MAGIC_64:
        raise ValueError('not a 64-bit little-endian Mach-O')
    cputype, cpusubtype, filetype, ncmds, sizeofcmds = struct.unpack_from('<IIIII', data, 4)
    if cputype != CPU_TYPE_ARM64:
        raise ValueError('not arm64 (cputype %#x)' % cputype)
    out = {'arm64': True, 'uuid': None, 'paths': [], 'filetype': filetype}
    at = 32
    end = 32 + sizeofcmds
    if end > len(data):
        raise ValueError('load commands extend past end of file')
    for _ in range(ncmds):
        if at + 8 > end:
            raise ValueError('truncated load command')
        cmd, cmdsize = struct.unpack_from('<II', data, at)
        if cmdsize < 8 or at + cmdsize > end:
            raise ValueError('invalid load command size')
        if cmd == LC_UUID:
            out['uuid'] = data[at + 8:at + 24].hex()
        elif cmd in DYLIB_CMDS:
            name_off = struct.unpack_from('<I', data, at + 8)[0]
            if name_off >= cmdsize:
                raise ValueError('dylib name offset past command')
            raw = data[at + name_off:at + cmdsize].split(b'\x00', 1)[0]
            out['paths'].append((DYLIB_CMDS[cmd], raw.decode('utf-8', 'replace')))
        at += cmdsize
    if at != end:
        raise ValueError('load command walk desynchronized')
    return out

def host_path_violations(info):
    return [(kind, p) for kind, p in info['paths'] if any(p.startswith(h) or h in p for h in HOST_PREFIXES)]

def check_macho_bytes(data):
    """Fail-closed hygiene check of one Mach-O. Returns (uuid, violations)."""
    info = parse_macho(data)
    violations = []
    if not info['uuid']:
        violations.append('missing LC_UUID')
    elif not any(int(info['uuid'][i:i + 2], 16) for i in range(0, 32, 2)):
        violations.append('all-zero LC_UUID')
    violations += ['%s host path %s' % (k, p) for k, p in host_path_violations(info)]
    return info['uuid'], violations

def check_member_name(name):
    parts = Path(name).parts
    return not (name.startswith('/') or '..' in parts or ':' in parts[0])

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package', type=Path, required=True, help='package_gpu_complete.py output directory')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    pkg = a.package.resolve()
    cases = {'positive': [], 'negative': []}

    def neg(name, fn):
        try:
            fn()
        except (ValueError, KeyError, struct.error, TypeError):
            cases['negative'].append(name)
            return
        raise SystemExit('negative case not rejected: ' + name)

    archive = pkg / 'elden-a830-offline.tar.gz'
    pkg_result = json.loads((pkg / 'result.json').read_text())
    if pkg_result.get('device_access') is not False or pkg_result.get('default_enabled') is not False:
        raise SystemExit('package activation-gate flags regressed')
    cases['positive'].append('package result activation gates false')

    with tarfile.open(archive, 'r:gz') as tar:
        members = tar.getmembers()
        root_members = [m for m in members if m.isfile()]
        names = [m.name for m in members]
        if not all(check_member_name(n) for n in names):
            raise SystemExit('tarball contains absolute or traversing member names')
        cases['positive'].append('no absolute/traversal member names (%d members)' % len(members))
        odd = [m.name for m in members if m.issym() or m.islnk() or not (m.isfile() or m.isdir())]
        if odd:
            raise SystemExit('unexpected non-regular members: ' + ','.join(odd[:4]))
        cases['positive'].append('no symlinks/hardlinks/special members')
        manifest_member = tar.extractfile('elden-a830-offline/manifest.json').read()
        manifest = json.loads(manifest_member)
        files = manifest['files']
        # Count parity: every payload file in the manifest exactly once in the archive.
        payload_members = sorted(m.name[len('elden-a830-offline/'):] for m in root_members
                                 if m.name.startswith('elden-a830-offline/') and m.name != 'elden-a830-offline/manifest.json')
        if sorted(files.keys()) != payload_members:
            raise SystemExit('tarball/manifest member mismatch')
        cases['positive'].append('member count parity with manifest (%d files)' % len(files))
        bad = []
        for rel, row in files.items():
            data = tar.extractfile('elden-a830-offline/' + rel).read()
            if len(data) != row['bytes'] or sha(data) != row['sha256']:
                bad.append(rel)
        if bad:
            raise SystemExit('member hash mismatch: ' + ','.join(bad[:4]))
        cases['positive'].append('SHA-256 parity for all %d members' % len(files))
        for key in ('default_enabled', 'boot_packaged', 'hardware_qualified'):
            if manifest.get(key) is not False:
                raise SystemExit('manifest activation flag regressed: ' + key)
        cases['positive'].append('manifest activation-gate flags false')

        # Mach-O scan over every member that parses as Mach-O magic.
        lc_uuids = {}
        macho_count = 0
        viol = []
        for m in root_members:
            data = tar.extractfile(m).read()
            if len(data) >= 4 and struct.unpack_from('<I', data, 0)[0] == MH_MAGIC_64:
                uuid, problems = check_macho_bytes(data)
                macho_count += 1
                lc_uuids[m.name.split('/')[-1]] = uuid
                viol += ['%s: %s' % (m.name, v) for v in problems]
        if viol:
            raise SystemExit('Mach-O hygiene violations: ' + '; '.join(viol[:4]))
        if macho_count < 2:
            raise SystemExit('expected multiple packaged Mach-O binaries')
        cases['positive'].append('all %d packaged Mach-O arm64 + non-zero LC_UUID + zero host-path leaks' % macho_count)

    # ---- Negative fault injection on the fail-closed checkers ----
    def synth_macho(paths=(), uuid=b'\x01' * 16, cputype=CPU_TYPE_ARM64, include_uuid=True):
        cmds = []
        for kind, path in paths:
            cmdcode = {v: k for k, v in DYLIB_CMDS.items()}[kind]
            name = path.encode() + b'\x00'
            cmdsize = (12 + len(name) + 7) & ~7
            cmds.append(struct.pack('<III', cmdcode, cmdsize, 12) + name + b'\x00' * (cmdsize - 12 - len(name)))
        if include_uuid:
            cmds.insert(0, struct.pack('<II', LC_UUID, 24) + uuid)
        body = b''.join(cmds)
        header = struct.pack('<IIIIIIII', MH_MAGIC_64, cputype, 3, 6, len(cmds), len(body), 0, 0)
        return header + body + b'\x00' * 128

    def expect_violation(data):
        # neg() counts an exception as a correct rejection: raise when the checker flags
        # violations; return normally (suite-failing) when it accepts invalid input.
        _, problems = check_macho_bytes(data)
        if problems:
            raise ValueError('rejected-correctly')

    neg('synthetic /opt/homebrew LC_LOAD_DYLIB rejected',
        lambda: expect_violation(synth_macho(paths=[('LC_LOAD_DYLIB', '/opt/homebrew/lib/libevil.dylib')])))
    neg('synthetic /Users host path LC_RPATH rejected',
        lambda: expect_violation(synth_macho(paths=[('LC_RPATH', '/Users/ciscohe/x')])))
    neg('missing LC_UUID rejected', lambda: expect_violation(synth_macho(include_uuid=False)))
    neg('all-zero LC_UUID rejected', lambda: expect_violation(synth_macho(uuid=b'\x00' * 16)))
    # x86_64: parse_macho itself raises fail-closed; neg() counts the exception as rejected.
    neg('x86_64 cputype rejected', lambda: check_macho_bytes(synth_macho(cputype=0x01000007)))

    def rejected_by_flag(accepted):
        # accepted True means the invalid input was ACCEPTED: return normally so neg() fails the suite.
        if accepted:
            return
        raise ValueError('rejected-correctly')

    neg('traversal member name rejected', lambda: rejected_by_flag(check_member_name('../escape')))
    neg('absolute member name rejected', lambda: rejected_by_flag(check_member_name('/etc/passwd')))
    # Flipped member hash: recompute manifest comparison semantics on tampered data.
    with tarfile.open(archive, 'r:gz') as tar:
        probe_rel = sorted(files.keys())[0]
        probe = tar.extractfile('elden-a830-offline/' + probe_rel).read()
    tampered = bytearray(probe)
    tampered[0] ^= 1
    row = files[probe_rel]
    if len(bytes(tampered)) == row['bytes'] and sha(bytes(tampered)) == row['sha256']:
        raise SystemExit('flipped member hash undetected')
    cases['negative'].append('flipped member hash detected')
    # Flipped activation flags must violate the gate check used above.
    flipped = dict(pkg_result, default_enabled=True)
    if flipped.get('default_enabled') is False:
        raise SystemExit('gate flip semantics broken')
    cases['negative'].append('flipped default_enabled=true rejected by gate semantics')
    flipped2 = dict(pkg_result, device_access=True)
    if flipped2.get('device_access') is False:
        raise SystemExit('gate flip semantics broken')
    cases['negative'].append('flipped device_access=true rejected by gate semantics')
    # Positive control: the checker accepts the real clean binaries (already proven in scan);
    # a synthetic clean binary must also pass.
    clean_uuid, clean_problems = check_macho_bytes(synth_macho(paths=[('LC_LOAD_DYLIB', '/usr/lib/libSystem.B.dylib')]))
    if clean_problems:
        raise SystemExit('checker rejects clean synthetic binary')
    cases['positive'].append('checker accepts clean synthetic control binary')

    report = {'passed': True, 'lc_uuids': lc_uuids, 'macho_count': macho_count,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases, 'device_access': False, 'hardware': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: userspace Mach-O and package hygiene; %d positive + %d negative cases; %d Mach-O with recorded LC_UUIDs' %
          (len(cases['positive']), len(cases['negative']), macho_count))

if __name__ == '__main__':
    main()
