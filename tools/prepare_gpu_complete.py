"""Export the complete locked GPU/board/firmware inputs for the Mac build.

References are read through Git objects; their checkouts are never changed.
The result contains licensed firmware and must remain outside the source repo.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('references', 'linux', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    commands = [
        ['prepare_gpu_tree.py', '--references', args.references, '--output', out/'tree'],
        ['derive_gpu_opp_inputs.py', '--linux', args.linux, '--output', out/'opps'],
        ['prepare_gpu_firmware.py', '--references', args.references, '--output', out/'firmware'],
    ]
    for name, *arguments in commands:
        subprocess.run([sys.executable, str(ROOT/'tools'/name), *map(str, arguments)], check=True)
    files = {p.relative_to(out).as_posix(): sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'prepared.json').write_text(json.dumps({
        'files': files, 'source_lock_sha256': hashlib.sha256((ROOT/'config/gpu-build.lock.json').read_bytes().replace(b'\r\n', b'\n')).hexdigest(),
        'firmware_lock_sha256': hashlib.sha256((ROOT/'config/gpu-firmware.lock.json').read_bytes().replace(b'\r\n', b'\n')).hexdigest(),
        'lock_text_format': 'LF',
        'device_access': False,
    }, indent=2) + '\n')
    print('Complete inputs exported and hashed; no compilation or device access')

if __name__ == '__main__':
    main()
