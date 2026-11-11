"""Mac: full embedded recovery plus external XNU entry in an exact48MiB FAT."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root=Path.cwd();work=root/'out/prerequisites-20261106/rescue48';work.mkdir(parents=True,exist_ok=True)
assets=root/'out/rescue-compressed-20261106';efi=root/'out/payload-loader-20261106';ufs=root/'out/ufs-stage2-20261106'
env=dict(os.environ);env['PATH']=str(root/'.cache/mtools/bin')+os.pathsep+env['PATH']
def run(*args):return subprocess.check_output(list(map(str,args)),env=env,stderr=subprocess.STDOUT)
private=json.loads((root/'out/rescue-20261106/inputs.json').read_text())
entries=[dict(id='twrp',title='TWRP rescue - experimental',type='efi',path='EldenPayloadLoader.efi',args='linux '+private['linux_args']),
 dict(id='current',title='External UniXNU - current',type='efi',path='EldenStage2.efi',args='current'),
 dict(id='previous',title='External UniXNU - previous',type='efi',path='EldenStage2.efi',args='previous'),
 dict(id='ufs',title='UEFI UFS read probe',type='efi',path='EldenUfsProbe.efi',args='inspect'),
 dict(id='memory',title='Memory planning information',type='efi',path='EldenPayloadLoader.efi',args='memory'),
 dict(id='reset',title='Restart',type='action',action='reset'),dict(id='shutdown',title='Power off',type='action',action='shutdown')]
lines=['version = 1','','[boot]','timeout = -1','fallback_timeout = 600','default = "reset"','','[kits]','autoload = false','','[ui]','scale = 4']
for entry in entries:lines+=['','[[entry]]']+[k+' = '+json.dumps(v) for k,v in entry.items()]
(work/'unixnu.toml').write_text('\n'.join(lines)+'\n')
fat=work/'payload.img';run('mformat','-C','-T',48*1024*1024//512,'-v','ELDENRAM','-i',fat,'::')
run('mmd','-i',fat,'::EFI','::EFI/UniXNU','::EFI/UniXNU/Rescue')
files=[(efi/'EldenPayloadLoader.efi','EldenPayloadLoader.efi'),(ufs/'EldenStage2.efi','EldenStage2.efi'),
 (ufs/'EldenUfsProbe.efi','EldenUfsProbe.efi'),(assets/'Image.efi.lzma','Rescue/Image.efi.lzma'),
 (assets/'recovery-initrd.cpio.lzma','Rescue/recovery-initrd.cpio.lzma'),(work/'unixnu.toml','unixnu.toml')]
manifest={}
for source,name in files:
 run('mcopy','-i',fat,source,'::EFI/UniXNU/'+name)
 data=run('mtype','-i',fat,'::EFI/UniXNU/'+name);assert data==source.read_bytes()
 manifest[name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
import struct
with fat.open('r+b') as stream:stream.seek(0x1c0);stream.write(b'ELDFDT01'+struct.pack('<IIQ',1,0,0))
run(sys.executable,root/'scripts/build_phase2_image.py','--kind','boot','--embed-payload','--entry-fdt-mailbox',
 '--fd',root/'out/phase3/old-fd-verified.bin','--payload',fat,'--output',work/'boot.img','--work',work,'--clang','/usr/bin/clang')
import gzip
boot=(work/'boot.img').read_bytes();size=struct.unpack_from('<I',boot,8)[0];raw=gzip.decompress(boot[4096:4096+size])
assert len(raw)==128+4*1024*1024+48*1024*1024
assert hashlib.sha256(raw[128:0x400080]).hexdigest()=='e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534'
(work/'unixnu-rescue48.bin').write_bytes(raw)
report=dict(raw_bytes=len(raw),raw_sha256=hashlib.sha256(raw).hexdigest(),payload_bytes=48*1024*1024,
 files=manifest,embedded_XNU=False,full_recovery_preserved=True,hardware_tested=False)
(work/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
