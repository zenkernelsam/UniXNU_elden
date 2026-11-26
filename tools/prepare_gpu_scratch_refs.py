"""Codified three-step scratch-references drill for prepare_gpu_complete (offline).

Reusable, read-only-safe automation for the environment problems exposed in gpu-opt-022:
1. manual --shared-equivalent construction (`.git/objects/info/alternates` + refs copy)
   when `git clone --shared` / upload-pack dies on large repos -- strictly read-only on
   the source repositories, never writes into research/;
2. cc01fa33-style bundle/object injection into the scratch mirror, verified fail-closed
   (missing/corrupt bundle or unreachable object => nonzero exit);
3. blob:none partial-clone promisor detection (e.g. tb322fc-linux-kernel) with an explicit
   refusal to chain `--shared` clones of such repos plus the direct `--linux` guidance.

`--self-test` builds synthetic temporary repos inside %TEMP% / /private/tmp only,
cleans them up afterwards, and never touches research/.
"""
from pathlib import Path
import argparse, json, os, shutil, subprocess, sys, tempfile

CC01FA33 = 'cc01fa331982983169b019f7cb0218e2f25143b5'
CC0A4E8 = 'cc0a4e8e57d20f4a4698753c2530875f2b10c0ca'


def git(repo, *args, check=True):
    return subprocess.check_output(['git', '-C', str(repo)] + list(args),
                                   stderr=subprocess.DEVNULL) if check else subprocess.run(
        ['git', '-C', str(repo)] + list(args), capture_output=True)


def is_git_repo(path):
    return (Path(path) / '.git').is_dir() or (Path(path) / 'objects').is_dir()


def gitdir_of(path):
    p = Path(path)
    if (p / '.git').is_dir():
        return p / '.git'
    if (p / 'objects').is_dir():  # bare
        return p
    return None


def partial_clone_info(repo):
    """Returns {'promisor': bool, 'filter': str|None} for the origin remote."""
    def cfg(key):
        r = subprocess.run(['git', '-C', str(repo), 'config', '--get', key], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None
    return {'promisor': cfg('remote.origin.promisor') == 'true',
            'filter': cfg('remote.origin.partialclonefilter')}


def detect_partial_failures(repo):
    """blob:none partial clones must not be chain-shared; direct --linux use is required."""
    info = partial_clone_info(repo)
    if info['promisor'] and info['filter'] and 'blob:none' in info['filter']:
        return ['blob:none partial clone; chained --shared scratch clones cannot lazy-fetch -- '
                'pass the original research path directly via --linux instead: %s' % repo]
    return []


def path_within(child, parent):
    try:
        Path(child).resolve().relative_to(Path(parent).resolve())
        return True
    except ValueError:
        return False


def build_shared_equivalent(src, dst):
    """Manual --shared clone: alternates + refs copy. Read-only on src.
    Returns list of failure strings (empty on success)."""
    src, dst = Path(src), Path(dst)
    if not src.is_dir():
        return ['source repo missing: %s' % src]
    gd = gitdir_of(src)
    if gd is None:
        return ['not a git repository: %s' % src]
    if path_within(dst, src) or path_within(src, dst):
        return ['refusing tamper: scratch path overlaps source repo']
    if dst.exists() and any(dst.iterdir()):
        return ['destination already populated: %s' % dst]
    info = partial_clone_info(src)
    if info['promisor'] and info['filter'] and 'blob:none' in info['filter']:
        return detect_partial_failures(src)
    dst.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', '-C', str(dst), 'init', '-q'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dg = dst / '.git'
    (dg / 'objects' / 'info').mkdir(parents=True, exist_ok=True)
    # Binary write: Windows text mode would emit CRLF and corrupt the alternates path.
    (dg / 'objects' / 'info' / 'alternates').write_bytes((str(gd / 'objects') + chr(10)).encode())
    for item in ('packed-refs', 'HEAD'):
        if (gd / item).is_file():
            shutil.copy2(gd / item, dg / item)
    if (gd / 'refs').is_dir():
        shutil.copytree(gd / 'refs', dg / 'refs', dirs_exist_ok=True)
    # Validate object visibility: HEAD must resolve.
    r = subprocess.run(['git', '-C', str(dst), 'rev-parse', '--verify', 'HEAD'], capture_output=True)
    if r.returncode != 0:
        return ['constructed scratch clone cannot resolve HEAD']
    return []


def inject_object(scratch_repo, bundle_path, expected_sha, helper_repo=None, helper_sha=None):
    """Inject a pinned object into the scratch mirror. Fail-closed: missing/corrupt bundle,
    unreachable object or sha mismatch all produce failures. When the object needs a
    predecessor (cc01fa33 needs cc0a4e8), helper_repo+helper_sha fetch it first."""
    repo, bundle = Path(scratch_repo), Path(bundle_path)
    if not repo.is_dir():
        return ['scratch repo missing: %s' % repo]
    if not bundle.is_file():
        return ['bundle missing: %s' % bundle]
    if helper_repo and helper_sha:
        r = subprocess.run(['git', '-C', str(repo), '-c', 'uploadpack.allowAnySHA1InWant=true',
                            'fetch', str(helper_repo), helper_sha], capture_output=True)
        if r.returncode != 0:
            return ['helper object fetch failed: %s' % helper_sha]
    heads = subprocess.run(['git', 'bundle', 'list-heads', str(bundle)], capture_output=True, text=True)
    if heads.returncode != 0:
        return ['bundle unreadable (corrupt): %s' % bundle]
    ref = None
    for line in heads.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            ref = parts[1]
            break
    if ref is None:
        return ['bundle carries no refs: %s' % bundle]
    r = subprocess.run(['git', '-C', str(repo), 'fetch', str(bundle.resolve()), ref], capture_output=True)
    if r.returncode != 0:
        return ['bundle fetch failed (corrupt or incompatible): %s' % bundle]
    r = subprocess.run(['git', '-C', str(repo), 'cat-file', '-t', expected_sha], capture_output=True, text=True)
    if r.returncode != 0 or r.stdout.strip() != 'commit':
        return ['injected object unreachable or wrong type: %s' % expected_sha]
    return []


def scratch_refs_failures(references, output, skip_partial=True):
    """Build a full scratch mirror of a references directory. Returns failures."""
    refs, out = Path(references), Path(output)
    if not refs.is_dir():
        return ['references root missing: %s' % refs]
    if path_within(out, refs):
        return ['refusing tamper: scratch output inside references root']
    failures = []
    for entry in sorted(refs.iterdir()):
        if not entry.is_dir() or gitdir_of(entry) is None:
            continue
        f = build_shared_equivalent(entry, out / entry.name)
        if f and skip_partial and partial_clone_info(entry)['promisor']:
            continue  # partial clones are consumed directly via their original paths
        failures.extend('%s: %s' % (entry.name, x) for x in f)
    return failures


def self_test():
    """Synthetic temp repos only; cleans up afterwards; never touches research/."""
    positive, negative, failures = [], [], []
    tmp = Path(tempfile.mkdtemp(prefix='scratch-refs-selftest-'))

    def make_repo(path, files=('a.txt',)):
        path.mkdir(parents=True)
        subprocess.run(['git', '-C', str(path), 'init', '-q'], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['git', '-C', str(path), 'config', 'user.email', 'self@test'], check=True)
        subprocess.run(['git', '-C', str(path), 'config', 'user.name', 'selftest'], check=True)
        for i, name in enumerate(files):
            (path / name).write_text('content-%d' % i)
            subprocess.run(['git', '-C', str(path), 'add', name], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(['git', '-C', str(path), 'commit', '-qm', 'c%d' % i], check=True)
        return path

    def expect_reject(name, fn):
        try:
            bad = fn()
        except Exception:
            bad = ['exception']
        if bad:
            negative.append(name)
        else:
            failures.append('negative case escaped: ' + name)

    try:
        src = make_repo(tmp / 'src-normal')
        # positive 1: alternates construction resolves HEAD and objects are visible
        dst = tmp / 'scratch1' / 'src-normal'
        f = build_shared_equivalent(src, dst)
        head_src = git(src, 'rev-parse', 'HEAD').decode().strip()
        head_dst = git(dst, 'rev-parse', 'HEAD').decode().strip() if not f else ''
        if not f and head_src == head_dst:
            positive.append('manual alternates construction resolves HEAD identically')
        else:
            failures.append('positive construction failed: ' + '; '.join(f))

        # positive 2: bundle injection makes object reachable in scratch mirror
        donor = make_repo(tmp / 'donor', ('x.txt', 'y.txt'))
        sha = git(donor, 'rev-parse', 'HEAD').decode().strip()
        bundle = tmp / 'donor.bundle'
        subprocess.run(['git', '-C', str(donor), 'bundle', 'create', str(bundle), 'HEAD'],
                       check=True, stdout=subprocess.DEVNULL)
        f = inject_object(dst, bundle, sha)
        if not f:
            positive.append('bundle injection object reachable in scratch mirror')
        else:
            failures.append('positive injection failed: ' + '; '.join(f))

        # positive 3: blob:none detection outputs direct --linux guidance
        partial = make_repo(tmp / 'src-partial')
        subprocess.run(['git', '-C', str(partial), 'config', 'remote.origin.promisor', 'true'], check=True)
        subprocess.run(['git', '-C', str(partial), 'config', 'remote.origin.partialclonefilter', 'blob:none'], check=True)
        f = detect_partial_failures(partial)
        if f and '--linux' in f[0]:
            positive.append('blob:none partial clone detected with direct --linux guidance')
        else:
            failures.append('partial detection missing guidance')

        # negative 1: missing source repo rejected
        expect_reject('missing source repo rejected',
                      lambda: build_shared_equivalent(tmp / 'does-not-exist', tmp / 's2' / 'x'))
        # negative 2: corrupt bundle rejected fail-closed
        corrupt = tmp / 'corrupt.bundle'
        corrupt.write_bytes(b'not a bundle at all')
        expect_reject('corrupt bundle rejected',
                      lambda: inject_object(dst, corrupt, sha))
        # negative 3: chained --shared of a blob:none partial clone refused
        expect_reject('chained shared clone of blob:none refused',
                      lambda: build_shared_equivalent(partial, tmp / 's3' / 'src-partial'))
        # negative 4: scratch-inside-source tamper attempt rejected
        expect_reject('scratch-inside-source tamper rejected',
                      lambda: build_shared_equivalent(src, src / 'scratch-inside'))
        # negative 5: unreachable object sha rejected
        expect_reject('unreachable object sha rejected',
                      lambda: inject_object(dst, bundle, '0' * 40))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    return positive, negative, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--references', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--opt2-repo', default='AdrenoMetal-a830-opt2')
    p.add_argument('--bundle', type=Path, help='cc01fa33 bundle path for injection')
    p.add_argument('--official-repo', type=Path, help='AdrenoMetal-official path for cc0a4e8 helper fetch')
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()

    if a.self_test:
        positive, negative, failures = self_test()
        report = {
            'passed': not failures,
            'case_counts': {'positive': len(positive), 'negative': len(negative)},
            'positive': positive, 'negative': negative, 'failures': failures,
            'device_access': False, 'hardware_qualified': False,
        }
        print(json.dumps(report, indent=2))
        if a.output:
            out = a.output.resolve()
            out.mkdir(parents=True, exist_ok=True)
            (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
        if failures:
            raise SystemExit('self-test failures: ' + '; '.join(failures))
        print('PASS: prepare_gpu_scratch_refs self-test; %d positive + %d negative'
              % (len(positive), len(negative)))
        return

    if not (a.references and a.output):
        raise SystemExit('--references and --output are required (or --self-test)')
    failures = scratch_refs_failures(a.references.resolve(), a.output.resolve())
    if a.bundle:
        opt2 = a.output.resolve() / a.opt2_repo
        failures += inject_object(opt2, a.bundle.resolve(), CC01FA33,
                                  helper_repo=a.official_repo.resolve() if a.official_repo else None,
                                  helper_sha=CC0A4E8 if a.official_repo else None)
    if failures:
        raise SystemExit('scratch refs construction failed: ' + '; '.join(failures))
    print('PASS: scratch references mirror constructed at %s' % a.output)


if __name__ == '__main__':
    main()
