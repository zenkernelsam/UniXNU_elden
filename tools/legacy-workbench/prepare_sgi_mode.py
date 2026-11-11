from pathlib import Path
import hashlib,importlib.util,json,zipfile
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation');p=r/'research/cpu-sgi-mode-20261111';p.mkdir(exist_ok=False)
spec=importlib.util.spec_from_file_location('deferred',d/'Deferred.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
cpu=module.apply((r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes())
old=r/'research/cpu-mmio-read-20261111';source=(old/'EldenSdamRecovery.inc').read_text();before=(old/'SmpGuard.inc').read_text();after=(d/'SmpGuard.inc').read_text();assert source.count(before)==1
files={'ArmCpu.cpp':cpu,'EldenSdamRecovery.inc':source.replace(before,after).encode(),**{n:(d/n).read_bytes() for n in ('SmpGuard.inc','GuardPolicy.h','Deferred.py','SgiMode.h')}}
manifest={'files':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()},'parent_source_manifest_sha256':hashlib.sha256((r/'research/cpu-guard-20261111/source.json').read_bytes()).hexdigest(),'capability_guarded':True,'scope':'TYPER2-capable DS1 only;disable groups/set nASSGI/restore before CPU1 release','hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
