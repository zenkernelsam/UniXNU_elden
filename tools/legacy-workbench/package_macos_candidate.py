"""Package a specified Mac kernel/ADT with the accepted FD, without device I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--adt', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--gop-console', action='store_true')
    p.add_argument('--config', type=Path, help='explicit menu configuration for this isolated packet')
    p.add_argument('--xnu-title', default='ELDEN 3 UFS PROBE')
    a = p.parse_args()
    root = Path.cwd()
    a.work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(root / '.cache/mtools/bin') + os.pathsep + env['PATH']
    log = []

    def run(*args):
        r = subprocess.run(list(map(str, args)), env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log.append({'command': list(map(str, args)), 'returncode': r.returncode, 'output': r.stdout})
        (a.work / 'package-log.json').write_text(json.dumps(log, indent=2) + '\n')
        print(r.stdout, end='', flush=True)
        r.check_returncode()

    payload = a.work / 'payload.img'
    shutil.copyfile(root / 'out/offline-20261105/payload.img', payload)
    for local, name in ((a.kernel, 'kernel.macho'), (a.adt, 'kernel.macho.adt')):
        run('mcopy', '-o', '-m', '-i', payload, local, '::EFI/UniXNU/Payloads/' + name)
    if a.config or a.gop_console:
        config = a.config.read_text() if a.config else (root / 'package/phase3-ufs-marker-unixnu.toml').read_text()
        if not a.config:
            config = config.replace('native-ufs-probe=2 ', 'elden-gop-console=1 -enable_kprintf_spam native-ufs-probe=2 ')
        path = a.work / 'unixnu.toml'
        path.write_text(config)
        run('mcopy', '-o', '-m', '-i', payload, path, '::EFI/UniXNU/unixnu.toml')
    run(sys.executable, root / 'scripts/darwinboot_preflight.py', '--kernel', a.kernel, '--adt', a.adt)
    image = a.work / 'boot.img'
    run(sys.executable, root / 'scripts/build_phase2_image.py', '--kind', 'boot', '--embed-payload',
        '--fd', root / 'out/phase3/old-fd-verified.bin', '--payload', payload, '--output', image,
        '--work', a.work, '--clang', '/usr/bin/clang')
    digest = hashlib.sha256(a.kernel.read_bytes()).hexdigest()
    run(sys.executable, root / 'scripts/verify_phase2_artifact.py', '--kind', 'boot',
        '--image', image, '--fd', root / 'out/phase3/old-fd-verified.bin',
        '--shim', a.work / 'shim-boot-embed.bin', '--embed-payload', '--payload', payload,
        '--expect-kernel-bytes', a.kernel.stat().st_size, '--expect-kernel-sha256', digest,
        '--xnu-title', a.xnu_title, '--report', a.work / 'verify.json')


if __name__ == '__main__':
    main()
