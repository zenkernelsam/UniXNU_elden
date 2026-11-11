"""Mac: package the built LUN0 read-check with the accepted FD and corrected ADT.

No signing/device operations. Keeps the 256MiB diagnostic memory limit and adds
the existing read-only UEFI memory report as a separate menu selection.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

root=Path.cwd()
build=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados'
result=json.loads((root/'out/ipados-console-20261107/result.json').read_text())
assert result['build_returncode']==0 and result['restore_returncode']==0
base=build/'kernel/kernel.macho';plan=json.loads(base.with_suffix('.json').read_text())
digest=hashlib.sha256(base.read_bytes()).hexdigest();assert plan['candidate_sha256']==digest
tree=build/'tree'
command=json.loads((tree/'build/sheng-ufs/compile-command.json').read_text())
for define in ('-DNATIVE_UFS_READ_ONLY=1','-DNATIVE_UFS_PROBE_ONLY=0','-DUFS_ROOT_PARTITION="ipados"'):
    assert define in command
source=(tree/'build/sheng-ufs/src/NativeUfs.cpp').read_text()
assert '__asm__ volatile ("ldr %w0, [%1]"' in source
adt=root/'out/arm-io-phandle-fix-20261106/kernel.macho.adt'
assert hashlib.sha256(adt.read_bytes()).hexdigest()=='464f1053f4f2c1afef03da4bc638a06c1401c5be799eb156d8b5078eb1fa0a42'
loader=root/'out/ipados-memory-loader.efi'
assert hashlib.sha256(loader.read_bytes()).hexdigest()=='ae8e5938b22766b921f43f68e2ea63db028fb57ec1f3791edaf7db10f5825299'
work=root/'out/ipados-console-entry-20261107'
work.mkdir(exist_ok=True)
sys.path.insert(0,str(root/'scripts'));import make_entry_probe_kernel as maker
offset=maker.locate(base.read_bytes(),0xfffffff004aaecfc)[1]
assert struct.unpack_from('<I',base.read_bytes(),offset)[0]==0xaa1503e0
subprocess.run([sys.executable,root/'scripts/make_entry_probe_kernel.py','--kernel',base,'--adt',adt,
    '--expect-kernel-bytes',str(base.stat().st_size),'--expect-kernel-sha256',digest,'--output-dir',work,
    '--clang','/usr/bin/clang','--poke','0xfffffff004aaecfc=0x17ffffd4'],check=True)
kernel=work/'probe/kernel.macho';payload=work/'payload.img'
env=dict(os.environ);env['PATH']=str(root/'.cache/mtools/bin')+':'+env['PATH']
shutil.copyfile(root/'out/offline-20261105/payload.img',payload)
for local,name in ((kernel,'Payloads/kernel.macho'),(kernel.with_suffix('.macho.adt'),'Payloads/kernel.macho.adt'),
                   (loader,'EldenPayloadLoader.efi'),(root/'package/elden-ipados-readcheck-unixnu.toml','unixnu.toml')):
    subprocess.run(['mcopy','-o','-m','-i',str(payload),str(local),'::EFI/UniXNU/'+name],env=env,check=True)
subprocess.run([sys.executable,root/'scripts/darwinboot_preflight.py','--kernel',kernel,'--adt',adt],check=True)
image=work/'boot.img'
subprocess.run([sys.executable,root/'scripts/build_phase2_image.py','--kind','boot','--embed-payload',
    '--fd',root/'out/phase3/old-fd-verified.bin','--payload',payload,'--output',image,'--work',work,'--clang','/usr/bin/clang'],check=True)
subprocess.run([sys.executable,root/'scripts/verify_phase2_artifact.py','--kind','boot','--image',image,
    '--fd',root/'out/phase3/old-fd-verified.bin','--shim',work/'shim-boot-embed.bin','--embed-payload','--payload',payload,
    '--expect-kernel-bytes',str(kernel.stat().st_size),'--expect-kernel-sha256',hashlib.sha256(kernel.read_bytes()).hexdigest(),
    '--xnu-title','ELDEN IPADOS ROOT CHECK - LUN0','--report',work/'verify.json'],env=env,check=True)
report=dict(source_kernel_sha256=digest,derivative_kernel_sha256=hashlib.sha256(kernel.read_bytes()).hexdigest(),
            adt_sha256=hashlib.sha256(adt.read_bytes()).hexdigest(),memory_efi_sha256=hashlib.sha256(loader.read_bytes()).hexdigest(),
            unsigned_boot_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),device_tested=False,
            limitation='Read-only root manifest and free-memory checks; not an iPadOS userland build')
(work/'packet.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
