"""Mac: package complete compressed XNU and recovery in the accepted 56 MiB FAT size."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import struct
import tomllib

root=Path.cwd()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--entry-fdt-mailbox',action='store_true')
parser.add_argument('--ufs-stage2',action='store_true',help='add explicit optional UFS inspect/current/previous entries')
parser.add_argument('--memory-entry',action='store_true')
parser.add_argument('--work',type=Path,help='explicit new output directory')
args=parser.parse_args()
if args.ufs_stage2 and not args.entry_fdt_mailbox:parser.error('--ufs-stage2 requires --entry-fdt-mailbox')
assets=root/'out/rescue-compressed-20261106'
work=root/('out/combined-rescue-fdt-20261106' if args.entry_fdt_mailbox else 'out/combined-rescue-20261106')
if args.ufs_stage2:work=root/'out/combined-stage2-20261106'
if args.work:work=args.work.resolve()
work.mkdir(parents=True,exist_ok=True)
env=dict(os.environ);env['PATH']=str(root/'.cache/mtools/bin')+os.pathsep+env['PATH']
log=[]
def run(*command):
    result=subprocess.run(list(map(str,command)),env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log.append(dict(command=list(map(str,command)),returncode=result.returncode,output=result.stdout))
    (work/'build-log.json').write_text(json.dumps(log,indent=2)+'\n')
    if result.returncode:
        print(result.stdout);raise SystemExit(result.returncode)

compression=json.loads((assets/'compression-report.json').read_text())
assert compression['compression_complete'] and compression['all_cpio_content_preserved']
for name,item in compression['assets'].items():
    assert hashlib.sha256((assets/(name+'.lzma')).read_bytes()).hexdigest()==item['compressed_sha256']
loader=root/'out/payload-loader-20261106/EldenPayloadLoader.efi'
loader_info=json.loads((loader.parent/'artifact.json').read_text())
assert hashlib.sha256(loader.read_bytes()).hexdigest()==loader_info['sha256']
adt=root/'out/gop-entry-20261106/probe/kernel.macho.adt'
assert hashlib.sha256(adt.read_bytes()).hexdigest()=='b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3'
menu=tomllib.loads((root/'package/elden-ufs-rootcheck-unixnu.toml').read_text())
lines=['version = 1','','[boot]','timeout = -1','fallback_timeout = 600','default = "reset"',
       '','[kits]','autoload = false','','[ui]','scale = 4']
for entry in menu['entry']:
    item=dict(entry)
    if item['type']=='xnu':
        item['type']='efi';item['path']='EldenPayloadLoader.efi';item['args']='xnu '+item['args']
    lines+=['','[[entry]]']+[key+' = '+json.dumps(value) for key,value in item.items()]
private=json.loads((root/'out/rescue-20261106/inputs.json').read_text())
assert 'bootconfig' in private['linux_args'].split()
rescue=dict(id='twrp',title='TWRP rescue - experimental',type='efi',path='EldenPayloadLoader.efi',args='linux '+private['linux_args'])
lines+=['','[[entry]]']+[key+' = '+json.dumps(value) for key,value in rescue.items()]
if args.memory_entry:
    memory=dict(id='memory',title='Memory planning information',type='efi',path='EldenPayloadLoader.efi',args='memory')
    lines+=['','[[entry]]']+[key+' = '+json.dumps(value) for key,value in memory.items()]
stage2_files=[]
if args.ufs_stage2:
    stage2=root/'out/ufs-stage2-20261106'
    built=json.loads((stage2/'artifact.json').read_text())
    for name in ('EldenUfsProbe.efi','EldenStage2.efi'):
        data=(stage2/name).read_bytes()
        assert hashlib.sha256(data).hexdigest()==built['applications'][name]['sha256']
        stage2_files.append((stage2/name,name))
    for entry in (
        dict(id='ufs-stage2-inspect',title='UEFI UFS read probe - experimental',type='efi',path='EldenUfsProbe.efi',args='inspect'),
        dict(id='stage2-current',title='External UniXNU - current',type='efi',path='EldenStage2.efi',args='current'),
        dict(id='stage2-previous',title='External UniXNU - previous',type='efi',path='EldenStage2.efi',args='previous')):
        lines+=['','[[entry]]']+[key+' = '+json.dumps(value) for key,value in entry.items()]
config=work/'unixnu.toml';config.write_text('\n'.join(lines)+'\n')
fat=work/'payload.img'
run('mformat','-C','-T',str(56*1024*1024//512),'-v','ELDENRAM','-i',fat,'::')
run('mmd','-i',fat,'::EFI','::EFI/UniXNU','::EFI/UniXNU/Payloads','::EFI/UniXNU/Rescue')
files=[(loader,'EldenPayloadLoader.efi'),(adt,'Payloads/kernel.macho.adt'),
       (assets/'kernel.macho.lzma','Payloads/kernel.macho.lzma'),
       (assets/'Image.efi.lzma','Rescue/Image.efi.lzma'),
       (assets/'recovery-initrd.cpio.lzma','Rescue/recovery-initrd.cpio.lzma'),(config,'unixnu.toml')]
files+=stage2_files
for source,target in files:
    run('mcopy','-i',fat,source,'::EFI/UniXNU/'+target)
if args.entry_fdt_mailbox:
    with fat.open('r+b') as stream:
        stream.seek(0x1c0);stream.write(b'ELDFDT01'+struct.pack('<IIQ',1,0,0))
command=[sys.executable,root/'scripts/build_phase2_image.py','--kind','boot','--embed-payload',
    '--fd',root/'out/phase3/old-fd-verified.bin','--payload',fat,'--output',work/'boot.img',
    '--work',work,'--clang','/usr/bin/clang']
if args.entry_fdt_mailbox:command.append('--entry-fdt-mailbox')
run(*command)
report=dict(status='experimental combined packet; hardware Linux/TWRP and decryption unverified',
    payload_bytes=fat.stat().st_size,image_bytes=(work/'boot.img').stat().st_size,
    image_sha256=hashlib.sha256((work/'boot.img').read_bytes()).hexdigest(),
    loader_sha256=loader_info['sha256'],files={target:dict(bytes=source.stat().st_size,sha256=hashlib.sha256(source.read_bytes()).hexdigest()) for source,target in files},
    compressed_assets=compression['assets'],entry_fdt_mailbox=args.entry_fdt_mailbox,
    optional_ufs_stage2=args.ufs_stage2,memory_entry=args.memory_entry,FD_rebuilt=False,hardware_tested=False)
(work/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:value for key,value in report.items() if key not in ('files','compressed_assets')},indent=2))
