from pathlib import Path
import hashlib,importlib.util,json,zipfile
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation');p=r/'research/cpu-groups-20261111';p.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('deferred',d/'Deferred.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
base=(r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes();cpu=module.apply(base)
old=r/'research/cpu-guard-20261111';text=(old/'EldenSdamRecovery.inc').read_text();before=(old/'SmpGuard.inc').read_text();after=(d/'SmpGuard.inc').read_text();assert text.count(before)==1
source=text.replace(before,after)
files={'ArmCpu.cpp':cpu,'EldenSdamRecovery.inc':source.encode(),**{n:(d/n).read_bytes() for n in ('Deferred.py','SmpGuard.inc','GuardPolicy.h','GicGroups.h')}}
manifest={'files':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()},'parent_source_manifest_sha256':hashlib.sha256((old/'source.json').read_bytes()).hexdigest(),'parent_kernel_sha256':'4704c33991c6ef316abdcf51416c281c5573eb4ec5cbd38d476441f7581a2f58','only_new_hardware_write':'GICD_CTLR bit0 after watchdog/lease; preserve other bits','hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
