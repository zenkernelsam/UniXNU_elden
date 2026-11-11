"""Freeze the v86 CPU implementation plus an opt-in bounded UI hold."""
from pathlib import Path
import hashlib,json,zipfile
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation')
old=r/'research/cpu-asm-fixed-20261111';p=r/'research/cpu-hold-20261111';p.mkdir(exist_ok=False)
sha=lambda b:hashlib.sha256(b).hexdigest()
q=old/'eight-core-qualification-v86.json';proof=json.loads(q.read_text());assert proof['qualified'] and proof['cpus']==8
for n,h in proof['evidence_sha256'].items():assert sha((r/n).read_bytes())==h,n
for n in ('GuardPolicy.h','Deferred.py'):assert (d/n).read_bytes()==(old/n).read_bytes()
guard=(d/'SmpGuard.inc').read_text()
base=(old/'EldenSdamRecovery.inc').read_text();oldguard=(old/'SmpGuard.inc').read_text()
assert base.count(oldguard)==1
# Keep the top-level recovery implementation byte-for-byte outside the guard.
guard=guard.replace('#include "HoldPolicy.h"',(d/'HoldPolicy.h').read_text())
recovery=base.replace(oldguard,guard)
files={n:(old/n).read_bytes() for n in ('ArmCpu.cpp','GuardPolicy.h','Deferred.py')}
files.update({n:(d/n).read_bytes() for n in ('SmpGuard.inc','HoldPolicy.h','TestHoldPolicy.c')})
files['EldenSdamRecovery.inc']=recovery.encode()
manifest={'files':{n:sha(b) for n,b in files.items()},'parent_source_manifest_sha256':sha((r/'research/cpu-guard-20261111/source.json').read_bytes()),'eight_core_qualification_sha256':sha(q.read_bytes()),'parent_v86_source_sha256':sha((old/'source.json').read_bytes()),'cpu_implementation_unchanged':True,'assembly_delimiters_fixed':True,'hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
