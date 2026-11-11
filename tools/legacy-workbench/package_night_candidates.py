"""Mac-only offline package of root/USB test candidates; no signing or device I/O."""
from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys
root=Path.cwd();out=root/'out/night-candidates-20261107';out.mkdir(exist_ok=True)
base=root/'out/usb-root-20261107/kernel.macho';report=json.loads((root/'out/usb-root-20261107/result.json').read_text())
sha=hashlib.sha256(base.read_bytes()).hexdigest();assert sha==report['kernel_sha256']
adt=out/'root.adt';trust=out/'original-static-system.bundle';config=root/'package/elden-night-test-unixnu.toml'
assert hashlib.sha256(trust.read_bytes()).hexdigest()=='7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7'
sys.path.insert(0,str(root/'scripts'));import make_entry_probe_kernel as maker
assert struct.unpack_from('<I',base.read_bytes(),maker.locate(base.read_bytes(),0xfffffff004aaecfc)[1])[0]==0xaa1503e0
plain_adt=root/'out/arm-io-phandle-fix-20261106/kernel.macho.adt'
assert hashlib.sha256(plain_adt.read_bytes()).hexdigest()=='464f1053f4f2c1afef03da4bc638a06c1401c5be799eb156d8b5078eb1fa0a42'
# The entry-stub builder accepts the plain 256MiB ADT only; its kernel transformation is
# unchanged. The completed payload below supplies the separately checked cache.
subprocess.run([sys.executable,root/'scripts/make_entry_probe_kernel.py','--kernel',base,'--adt',plain_adt,
    '--expect-kernel-bytes',str(base.stat().st_size),'--expect-kernel-sha256',sha,'--output-dir',out/'common',
    '--clang','/usr/bin/clang','--poke','0xfffffff004aaecfc=0x17ffffd4'],check=True)
kernel=out/'common/probe/kernel.macho';env=dict(os.environ);env['PATH']=str(root/'.cache/mtools/bin')+':'+env['PATH']
for name in ('manual','autostart'):
    work=out/name;work.mkdir(exist_ok=True);payload=work/'payload.img'
    shutil.copyfile(root/'out/ipados-console-entry-20261107/payload.img',payload)
    menu=config.read_text()
    if name=='autostart':menu=menu.replace('timeout = 120','timeout = 8').replace('default = "shutdown"','default = "xnu"')
    (work/'unixnu.toml').write_text(menu)
    for local,target in ((kernel,'Payloads/kernel.macho'),(adt,'Payloads/kernel.macho.adt'),
                         (trust,'Payloads/kernel.macho.trustcache'),(work/'unixnu.toml','unixnu.toml')):
        subprocess.run(['mcopy','-o','-m','-i',str(payload),str(local),'::EFI/UniXNU/'+target],env=env,check=True)
    image=work/'boot.img'
    subprocess.run([sys.executable,root/'scripts/build_phase2_image.py','--kind','boot','--embed-payload',
        '--fd',root/'out/phase3/old-fd-verified.bin','--payload',payload,'--output',image,'--work',work,'--clang','/usr/bin/clang'],check=True)
    subprocess.run([sys.executable,root/'scripts/verify_phase2_artifact.py','--kind','boot','--image',image,
        '--fd',root/'out/phase3/old-fd-verified.bin','--shim',work/'shim-boot-embed.bin','--embed-payload','--payload',payload,
        '--expect-kernel-bytes',str(kernel.stat().st_size),'--expect-kernel-sha256',hashlib.sha256(kernel.read_bytes()).hexdigest(),
        '--xnu-title','iPadOS root + USB logs - TEST','--expect-timeout','8' if name=='autostart' else '120',
        '--expect-default','xnu' if name=='autostart' else 'shutdown','--report',work/'verify.json'],env=env,check=True)
    r=dict(unsigned_boot_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),kernel_sha256=hashlib.sha256(kernel.read_bytes()).hexdigest(),
           adt_sha256=hashlib.sha256(adt.read_bytes()).hexdigest(),trust_sha256=hashlib.sha256(trust.read_bytes()).hexdigest(),
           mode=name,hardware_tested=False,autonomous_fastboot_recovery=False)
    (work/'packet.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
