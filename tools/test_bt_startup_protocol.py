"""Run frozen v115 wake or v117 debug-ACL protocol fixtures against pinned inputs."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def linux_path(path):
    path = path.resolve()
    if len(path.drive) != 2:
        raise ValueError('Windows drive required for WSL tests')
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

def main():
    if sys.flags.optimize:
        raise SystemExit('Assertions required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('v115-ibs-wake', 'v117-hci-debug'), required=True)
    for name in ('dependency', 'firmware', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Fresh output required')
    output.mkdir(parents=True)
    reference = output / 'reference'
    reference.mkdir()
    plan = json.loads((ROOT / 'profiles/v110-bt-fifo/plan.json').read_text())
    for name in ('BtH4.c', 'BtH4.h', 'BtTlv.c', 'BtTlv.h', 'BtUtil.h'):
        item = plan['dependency']['files'][name]
        data = subprocess.check_output(['git', '-C', str(args.dependency), 'show',
                                       plan['dependency']['revision'] + ':' + item['path']])
        assert hashlib.sha256(data).hexdigest() == item['sha256']
        (reference / name).write_bytes(data)
    for item in plan['firmware']:
        assert sha(args.firmware / item['file']) == item['sha256']
    src = ROOT / 'profiles' / args.profile / 'sources'
    bt = ROOT / 'drivers/EldenBluetooth'
    startup = ROOT / 'profiles/v115-ibs-wake/sources/Wcn7861Startup.c'
    hci = ROOT / ('profiles/v114-hci-length/sources/Wcn7861Hci.c'
                  if args.profile == 'v115-ibs-wake' else 'profiles/v117-hci-debug/sources/Wcn7861Hci.c')
    results = {}
    for kind in ('startup', 'hci'):
        files = [src / ('TestWcn7861Startup.c' if kind == 'startup' else 'TestWcn7861Hci.c'),
                 startup, bt / 'Wcn7861Peripheral.c', reference / 'BtH4.c', reference / 'BtTlv.c']
        if kind == 'hci':
            files.append(hci)
        command = ['wsl', '-d', 'Ubuntu-22.04', '-u', 'root', '--', 'cc', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for path in (src, bt, reference):
            command += ['-I', linux_path(path)]
        command += [linux_path(path) for path in files] + ['-o', linux_path(output / ('test-' + kind))]
        result = subprocess.run(command, capture_output=True, timeout=90)
        (output / (kind + '-build.log')).write_bytes(result.stdout + result.stderr)
        result.check_returncode()
        names = ['qca/brhperifw20.tlv', 'qca/brhperinv20.bin', 'qca/tmel_peach_20.elf']
        if kind == 'hci':
            names += ['qca/brhbtfw20.tlv', 'qca/lenovo/tb322fc/brhbtnv20.bin']
        run = ['wsl', '-d', 'Ubuntu-22.04', '-u', 'root', '--', 'env',
               'ASAN_OPTIONS=detect_leaks=1', 'UBSAN_OPTIONS=halt_on_error=1', command[-1]]
        run += [linux_path(args.firmware / name) for name in names]
        result = subprocess.run(run, capture_output=True, timeout=90)
        (output / (kind + '-run.log')).write_bytes(result.stdout + result.stderr)
        result.check_returncode()
        assert b'PASS' in result.stdout
        results[kind] = {'command': command, 'run': run, 'output': result.stdout.decode(),
                         'inputs': {str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path):
                                    sha(path) for path in files}}
        print(kind + ': ' + result.stdout.decode().strip())
    (output / 'result.json').write_text(json.dumps({'profile': args.profile, 'results': results,
                                                   'hardware_verified': False}, indent=2) + '\n')

if __name__ == '__main__':
    main()
