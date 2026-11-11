"""Derive a new watchdog trial from the exact v77 upload."""
from pathlib import Path
import argparse,hashlib,json,zipfile,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,choices=[79,80],default=79);version=parser.parse_args().version
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation')
old=r/'edl_work/basic-bundle-20261108/v77';out=r/f'edl_work/basic-bundle-20261108/v{version}';out.mkdir(exist_ok=False)
sha=lambda b:hashlib.sha256(b).hexdigest()
archive=old/'source-upload.zip';assert sha(archive.read_bytes())=='f31b69c4c439e29151a4f53a28b61fc6027c5d310c8721b3d1ec8fdd9d535363'
with zipfile.ZipFile(archive) as z:
    manifest=json.loads(z.read('manifest.json'));inputs={n:z.read(n) for n in manifest}
    assert all(sha(inputs[n])==h for n,h in manifest.items())
name='scripts/driver-patches/EldenSdamRecovery.inc';base=inputs[name]
assert sha(base)=='8a0796b60bae23730ed92de2b1fc9d1a7b68a0a10e7d5efb87222ee820df12ba'
parts={n:(d/n).read_bytes() for n in ('GunyahWatchdog.h','GunyahWatchdog.c','GunyahSmc.c','WatchdogTrial.inc')}
combined=parts['GunyahWatchdog.h'].decode().replace('#pragma once\n','')
for n in ('GunyahWatchdog.c','GunyahSmc.c'):combined+='\n'+parts[n].decode().replace('#include "GunyahWatchdog.h"\n','')
combined+='\n'+parts['WatchdogTrial.inc'].decode()
text=base.decode();anchor='static void eldenStartRecoveryDeadline(void);';assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\nstatic void eldenStartVdogTrial(void);')
anchor='  eldenStartRecoveryDeadline();';assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n  eldenStartVdogTrial();')
end=text.rfind('#endif');assert end>0
text=text[:end]+'\n/* ELDEN_VDOG_TRIAL_BEGIN */\n'+combined+'\n/* ELDEN_VDOG_TRIAL_END */\n'+text[end:]
inputs[name]=text.encode()
script=inputs.pop('out/build_basic_bundle-v77.py').decode().replace('startup-basic-bundle-v77-',f'startup-basic-bundle-v{version}-')
inputs[f'out/build_basic_bundle-v{version}.py']=script.encode()
(out/'build_basic_bundle.py').write_text(script,newline='\n')
new=out/'source-upload.zip'
with zipfile.ZipFile(new,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in inputs.items():z.writestr(n,b)
    z.writestr('manifest.json',json.dumps({n:sha(b) for n,b in inputs.items()},indent=2)+'\n')
installer=(old/'install.py').read_text().replace('v77',f'v{version}').replace('f31b69c4c439e29151a4f53a28b61fc6027c5d310c8721b3d1ec8fdd9d535363',sha(new.read_bytes()))
(out/'install.py').write_text(installer,newline='\n')
proof={'base_upload_sha256':sha(archive.read_bytes()),'zip_sha256':sha(new.read_bytes()),'parts':{n:sha(b) for n,b in parts.items()},'recovery_source_sha256':sha(inputs[name]),'only_runtime_changed':name,'version':version,'initial_pet':1,'hardware_tested':False}
(out/'vdog-source.json').write_text(json.dumps(proof,indent=2)+'\n')
remote='ciscohe@192.168.68.54:/Users/ciscohe/Desktop/UniXNU_elden/out/'
subprocess.run(['scp',str(new),remote+f'basic-bundle-source-upload-v{version}.zip'],check=True)
subprocess.run(['scp',str(out/'install.py'),remote+f'install_basic_bundle-v{version}.py'],check=True)
subprocess.run(['ssh','-o','BatchMode=yes','ciscohe@192.168.68.54',f'cd /Users/ciscohe/Desktop/UniXNU_elden && .venv/bin/python out/install_basic_bundle-v{version}.py'],check=True)
print(json.dumps(proof,indent=2))
