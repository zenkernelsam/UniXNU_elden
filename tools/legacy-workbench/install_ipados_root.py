"""Fixed Y700 root image installation; default only validates local inputs.

--check-device validates root and current partition identity without block writes.
--write streams the reviewed image to the separately compiled bounded writer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time
from device_target import ADB, SERIAL
from root_image_preflight import decode_header, file_hash

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'edl_work/root-install-20261107'
IMAGE = ROOT / 'edl_work/prerequisites-20261106/root-prepared/root.img'
SHA = 'ae3c0e19e224c01a2291d1cd58da337062fd1240a3a089af1ccead4f591884c8'
PREFIX = [str(ADB), '-s', SERIAL]
REMOTE = '/data/local/tmp/elden-write-ipados-root'

def run(args, **kw):
    return subprocess.run(args, check=True, capture_output=True, timeout=45, **kw)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    g = p.add_mutually_exclusive_group()
    g.add_argument('--check-device', action='store_true')
    g.add_argument('--write', action='store_true')
    a = p.parse_args()
    if a.write:p.error("Whole-file staging failed; use install_ipados_root_chunks.py for the reviewed write path.")
    OUT.mkdir(exist_ok=True)
    if IMAGE.stat().st_size != 9381609472 or file_hash(IMAGE) != SHA:
        raise ValueError('root input size/SHA mismatch')
    with IMAGE.open('rb') as f:
        layout = decode_header(f.read(4096), 137439981568)
    if layout['image_bytes'] != IMAGE.stat().st_size:
        raise ValueError('layout/image length mismatch')
    report = dict(serial=SERIAL, image_sha256=SHA, layout=layout, device_written=False)
    print('Local input SHA and SHNGROOT windows verified.', flush=True)
    if not (a.check_device or a.write):
        print(json.dumps(report, indent=2));return
    uid = run([*PREFIX, 'shell', 'su -c '+shlex.quote('id -u')]).stdout.strip()
    if uid != b'0':raise RuntimeError('root required')
    writer = OUT / 'write-ipados-root'
    report['writer_sha256'] = hashlib.sha256(writer.read_bytes()).hexdigest()
    run([*PREFIX, 'push', str(writer), REMOTE])
    run([*PREFIX, 'shell', 'chmod 700 '+shlex.quote(REMOTE)])
    check = run([*PREFIX, 'shell', 'su -c '+shlex.quote(REMOTE+' --check')])
    report['admission'] = check.stdout.decode()
    if 'TARGET VERIFIED' not in report['admission']:raise RuntimeError('no admission evidence')
    (OUT/'admission.json').write_text(json.dumps(report, indent=2)+'\n')
    print(report['admission'], flush=True)
    if not a.write:return
    attempt=time.strftime('%Y%m%d-%H%M%S')
    log = OUT/('device-write-'+attempt+'.log')
    started=time.monotonic()
    # Android /data has room for disk staging. Do not use this on a TWRP RAM
    # filesystem. ADB sync is binary-safe; Windows shell stdin is not (Ctrl-Z).
    recovery=run([*PREFIX,'shell','getprop ro.twrp.version']).stdout.strip()
    if recovery:raise RuntimeError('disk staging requires normal Android')
    space=run([*PREFIX,'shell','df -k /data/local/tmp']).stdout.decode().splitlines()
    if int(space[-1].split()[3])*1024 < IMAGE.stat().st_size+(1<<30):
        raise RuntimeError('insufficient staging disk space')
    stage='/data/local/tmp/elden-root-'+attempt+'.img'
    run([*PREFIX,'shell','test ! -e '+stage])
    print('Staging via binary-safe adb push onto Android /data.',flush=True)
    pushed=subprocess.run([*PREFIX,'push','-Z',str(IMAGE),stage],check=True,capture_output=True,timeout=900)
    (OUT/('adb-push-'+attempt+'.log')).write_bytes(pushed.stdout+pushed.stderr)
    staged_hash=run([*PREFIX,'shell','sha256sum '+stage]).stdout.decode().split()[0]
    if staged_hash!=SHA:raise RuntimeError('staged input digest mismatch')
    report.update(staged_file=stage,staged_sha256=staged_hash)
    print('Staged file SHA verified. Writing bounded ipados target.',flush=True)
    command=REMOTE+' --write-image < '+stage
    with log.open('xb') as output:
        process = subprocess.Popen([*PREFIX,'shell','-T','su -c '+shlex.quote(command)],
                                   stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in process.stdout:
            output.write(line);output.flush();print(line.decode(errors='replace').rstrip(),flush=True)
        rc=process.wait()
    text=log.read_text(errors='replace')
    if rc or 'IPADOS_ROOT_FULL_SHA256_VERIFIED '+SHA not in text:
        raise RuntimeError('device write failed or verification incomplete; retain log')
    report.update(device_written=True, complete_device_sha256_verified=True, seconds=time.monotonic()-started)
    (OUT/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    run([*PREFIX,'shell','rm -- '+stage])
    print('Root image installed and fully verified.', flush=True)

if __name__=='__main__':main()
