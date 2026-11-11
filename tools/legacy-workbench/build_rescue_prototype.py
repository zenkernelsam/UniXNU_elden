"""Mac-side isolated UniXNU/XNU/TWRP menu prototype. No device access.

Uses a larger RAM FAT volume; preserve the known first-shot image separately.
This verifies bytes and interfaces, not hardware boot or decryption support.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path.cwd()
    work = root / 'out/rescue-20261106'
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(root / '.cache/mtools/bin') + os.pathsep + env['PATH']
    report = []

    def run(*args):
        r = subprocess.run(list(map(str, args)), env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        report.append({'command':list(map(str,args)),'returncode':r.returncode,'output':r.stdout})
        (work/'build-log.json').write_text(json.dumps(report,indent=2)+'\n')
        print(r.stdout,end='',flush=True)
        r.check_returncode()

    source = json.loads((work/'inputs.json').read_text())
    for name, field in (('Image','kernel'),('recovery-initrd.img','initrd')):
        assert hashlib.sha256((work/name).read_bytes()).hexdigest() == source[field]['sha256']
    payload = work/'payload.img'
    run('mformat','-C','-F','-T',str(160*1024*1024//512),'-v','ELDENRAM','-i',payload,'::')
    run('mmd','-i',payload,'::EFI','::EFI/UniXNU','::EFI/UniXNU/Payloads','::EFI/UniXNU/Rescue')
    kernel = root/'out/gop-entry-20261106/probe/kernel.macho'
    adt = kernel.with_suffix('.macho.adt')
    for src, dest in ((kernel,'Payloads/kernel.macho'),(adt,'Payloads/kernel.macho.adt'),
                      (work/'Image','Rescue/Image'),(work/'recovery-initrd.img','Rescue/recovery-initrd.img')):
        run('mcopy','-m','-i',payload,src,'::EFI/UniXNU/'+dest)
    config = (root/'package/phase3-ufs-marker-unixnu.toml').read_text()
    config = config.replace('native-ufs-probe=2 ', 'elden-gop-console=1 -enable_kprintf_spam native-ufs-probe=2 ')
    config += '\n[[entry]]\nid = "twrp"\ntitle = "TWRP rescue prototype"\ntype = "linux"\n'
    config += 'path = "Rescue/Image"\ninitrd = "Rescue/recovery-initrd.img"\n'
    config += 'args = ' + json.dumps(source['linux_args']) + '\n'
    (work/'unixnu.toml').write_text(config)
    run('mcopy','-m','-i',payload,work/'unixnu.toml','::EFI/UniXNU/unixnu.toml')
    run(sys.executable,root/'scripts/build_phase2_image.py','--kind','boot','--embed-payload',
        '--fd',root/'out/phase3/old-fd-verified.bin','--payload',payload,'--output',work/'boot.img',
        '--work',work,'--clang','/usr/bin/clang')
    run(sys.executable,root/'scripts/verify_phase2_artifact.py','--kind','boot','--image',work/'boot.img',
        '--fd',root/'out/phase3/old-fd-verified.bin','--shim',work/'shim-boot-embed.bin',
        '--embed-payload','--payload',payload,'--expect-kernel-bytes',kernel.stat().st_size,
        '--expect-kernel-sha256',hashlib.sha256(kernel.read_bytes()).hexdigest(),
        '--xnu-title','ELDEN 3 UFS PROBE','--report',work/'verify.json')
    for name, relative in (('Image','Rescue/Image'),('recovery-initrd.img','Rescue/recovery-initrd.img')):
        copied = work/(name+'.roundtrip')
        run('mcopy','-o','-i',payload,'::EFI/UniXNU/'+relative,copied)
        assert copied.read_bytes() == (work/name).read_bytes()
    run('mdir','-i',payload,'-/','::')


if __name__ == '__main__':
    main()
