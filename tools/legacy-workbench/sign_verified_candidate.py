"""Sign a local boot candidate and publish its name only after independent AVB checks."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from avb_guard import check_image, metadata

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--openssl', type=Path)
    a = p.parse_args()
    source, destination = a.input.resolve(), a.output.resolve()
    if source == destination:
        raise SystemExit('input and published output must differ')
    env = dict(os.environ)
    candidates = [a.openssl, shutil.which('openssl'),
                  Path('C:/Program Files/Git/usr/bin/openssl.exe') if os.name == 'nt' else None]
    executable = next((Path(path).resolve() for path in candidates if path and Path(path).is_file()), None)
    if executable is None:
        raise SystemExit('provide --openssl pointing to a working local OpenSSL executable')
    version = subprocess.run([executable, 'version'], capture_output=True, text=True, timeout=10)
    if version.returncode or not version.stdout.startswith('OpenSSL '):
        raise SystemExit('OpenSSL version check failed')
    env['PATH'] = str(executable.parent) + os.pathsep + env['PATH']
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=destination.stem+'-signing-', suffix='.partial.img', dir=destination.parent)
    os.close(handle)
    staged = Path(temporary)
    try:
        result = subprocess.run([sys.executable, ROOT/'research/sign-probe-bootimg.py', source, staged],
                                env=env, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit('signer failed; published output unchanged\n'+result.stderr[-1800:]+result.stdout[-1800:])
        admitted = check_image(staged, 'boot_a')
        if admitted.bytes != 96*1024*1024:
            raise SystemExit('signed boot image must fill the 96 MiB partition')
        if metadata(staged)['props'] != metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']:
            raise SystemExit('boot properties differ from the accepted stock template')
        os.replace(staged, destination)
        report = dict(path=str(destination), bytes=admitted.bytes, sha256=admitted.sha256,
            index=admitted.index, location=admitted.location, algorithm='SHA256_RSA4096',
            independent_RSA_and_data_digest=True, stock_properties_equal=True,
            openssl_version=version.stdout.strip(), device_tested=False)
        destination.with_suffix('.signing.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(report, indent=2))
    finally:
        staged.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
