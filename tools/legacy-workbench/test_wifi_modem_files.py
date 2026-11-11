"""Exercise actual C read code on stock modem_a, then hash all outputs."""
import hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
OUT=ROOT/'edl_work/wifi-modem-files-20261108';OUT.mkdir(exist_ok=True)
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
source=DEVICE/'drivers/EldenWifi';fat=DEVICE/'drivers/EldenPmicGlink'
test=OUT/'test-files';outputs=OUT/'outputs';outputs.mkdir(exist_ok=True)
command=['wsl','-d','Ubuntu-22.04','-u','root','--','gcc','-std=c11','-Wall','-Wextra','-Werror',
         '-fsanitize=address,undefined','-g','-O1','-I',wsl(fat),wsl(source/'ModemFiles.c'),
         wsl(source/'TestModemFiles.c'),wsl(fat/'FatVolume.c'),'-o',wsl(test)]
subprocess.run(command,check=True)
r=subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--',wsl(test),wsl(ROOT/'Backup/B_edl_stock/lun4/modem_a.bin'),wsl(outputs)],capture_output=True,check=True)
print(r.stdout.decode(),end='')
paths=['image/peach/amss20.bin','image/peach/phy_ucode20.elf','image/peach/aux_ucode20.elf',
       'image/tmel_peach_20.elf','image/peach/data20.msc','image/peach/regdb.bin','image/peach/bdwlan.elf']
catalog=json.loads((ROOT/'research/wifi-elden-20261108/inputs.json').read_text())['firmware_assets'];report=[]
for i,path in enumerate(paths):
    expected=next(e for e in catalog if e['path']=='/'+path);b=(outputs/(str(i)+'.bin')).read_bytes()
    assert len(b)==expected['bytes'] and hashlib.sha256(b).hexdigest()==expected['sha256'],path
    report.append(dict(path=path,bytes=len(b),sha256=expected['sha256']))
header='/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n#pragma once\n'
header+='/* Original stock firmware fingerprints, no firmware bytes or device identifiers. */\n'
header+='static const struct {const char *path;unsigned bytes;const char *sha256;} elden_wifi_firmware_inputs[]={\n'
header+=''.join('    {%s,%u,%s},\n'%(json.dumps(x['path']),x['bytes'],json.dumps(x['sha256'])) for x in report)+'};\n'
(DEVICE/'package/EldenWifiFirmwareInputs.h').write_text(header,encoding='utf-8',newline='\n')
result=dict(accepted=True,real_modem_backup=True,hardware_tested=False,asan_ubsan=True,stdout=r.stdout.decode(),files=report,
            command=command,source_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'ModemFiles.c',source/'ModemFiles.h',source/'TestModemFiles.c',fat/'FatVolume.c']})
(OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS7 original firmware file sizes/SHA; generated board fingerprint header')
