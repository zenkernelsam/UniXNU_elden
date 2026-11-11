"""Mac: make an external FAT bundle using the exact accepted UniXNU app."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

root=Path.cwd()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work',type=Path)
parser.add_argument('--loader',type=Path,help='explicit verified new loader; other complete assets remain unchanged')
args=parser.parse_args()
work=args.work.resolve() if args.work else root/'out/external-stage2-20261106';work.mkdir(parents=True,exist_ok=True)
source=root/'out/combined-rescue-fdt-20261106'
accepted=root/'out/ufs-stage2-20261106/UniXNU-accepted.efi'
assert hashlib.sha256(accepted.read_bytes()).hexdigest()=='faa98d766a279a2057121833bd3636f339ba774f6c578363234807d42b7a498e'
env=dict(os.environ);env['PATH']=str(root/'.cache/mtools/bin')+os.pathsep+env['PATH']
def run(*args):return subprocess.check_output(list(map(str,args)),env=env)
manifest=json.loads((source/'manifest.json').read_text())
fat=work/'stage2.fat';shutil.copyfile(source/'payload.img',fat)
for name,info in manifest['files'].items():
    data=run('mtype','-i',fat,'::EFI/UniXNU/'+name)
    assert len(data)==info['bytes'] and hashlib.sha256(data).hexdigest()==info['sha256'],name
menu=tomllib.loads(run('mtype','-i',fat,'::EFI/UniXNU/unixnu.toml').decode())
lines=['version = 1','','[boot]','timeout = -1','fallback_timeout = 600',
       'default = "safe"','connect_all = false','','[kits]','autoload = false','','[ui]','scale = 4']
for entry in menu['entry']:
    item=dict(entry)
    if item['id']=='xnu':item['title']='EXTERNAL STAGE2 - UFS read check'
    lines+=['','[[entry]]']+[key+' = '+json.dumps(value) for key,value in item.items()]
lines+=['','[[entry]]','id = "safe"','title = "Restart fixed rescue menu"','type = "action"','action = "exit"']
config=work/'unixnu.toml';config.write_text('\n'.join(lines)+'\n')
run('mmd','-i',fat,'::EFI/BOOT')
run('mcopy','-i',fat,accepted,'::EFI/BOOT/BOOTAA64.EFI')
run('mcopy','-o','-i',fat,config,'::EFI/UniXNU/unixnu.toml')
if args.loader:run('mcopy','-o','-i',fat,args.loader.resolve(),'::EFI/UniXNU/EldenPayloadLoader.efi')
# External RAM FAT is not the BootShim entry volume; inherit the table that
# stage1 publishes after releasing its UFS session, never a stale mailbox.
with fat.open('r+b') as stream:stream.seek(0x1c0);stream.write(bytes(24))
files={}
for name in manifest['files']:
    data=run('mtype','-i',fat,'::EFI/UniXNU/'+name)
    files['EFI/UniXNU/'+name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    if name=='EldenPayloadLoader.efi' and args.loader:assert data==args.loader.read_bytes()
    elif name!='unixnu.toml':assert files['EFI/UniXNU/'+name]==manifest['files'][name]
app=run('mtype','-i',fat,'::EFI/BOOT/BOOTAA64.EFI');assert app==accepted.read_bytes()
files['EFI/BOOT/BOOTAA64.EFI']=dict(bytes=len(app),sha256=hashlib.sha256(app).hexdigest())
report=dict(bundle_bytes=fat.stat().st_size,bundle_sha256=hashlib.sha256(fat.read_bytes()).hexdigest(),
            files=files,menu_entries=len(menu['entry'])+1,current_boot_FDT='inherited EFI table',
            source_manifest_sha256=hashlib.sha256((source/'manifest.json').read_bytes()).hexdigest(),
            arbitrary_stage2_return='cold reset to fixed bootstrap',
            hardware_tested=False,source_UniXNU='exact accepted old FD application')
(work/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
