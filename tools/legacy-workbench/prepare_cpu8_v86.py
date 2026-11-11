"""Reuse exact v86 two-core kernel/qualifications; change only admission CPU count."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'edl_work/basic-bundle-20261108/v86/final'
source=out/'icon-cpu-focused-gestures-ipad-pro-cpu2'
target=out/'icon-cpu-focused-gestures-ipad-pro-cpu8'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
target.mkdir(exist_ok=True)
copied={}
for path in source.glob('*-qualification.json'):
 dest=target/path.name
 if dest.exists():assert dest.read_bytes()==path.read_bytes(),dest
 else:dest.write_bytes(path.read_bytes())
 copied[path.name]=sha(path)
subprocess.run([sys.executable,ROOT/'edl_work/package_basic_bundle.py','--version','86','--default','icon-cpu','--focused','--gestures','--ipad-pro','--cpu-guard-cpus','8'],check=True)
import tomllib
before=tomllib.loads((source/'unixnu.toml').read_text());after=tomllib.loads((target/'unixnu.toml').read_text())
def normalize(value):
 if isinstance(value,dict):return {k:normalize(v) for k,v in value.items()}
 if isinstance(value,list):return [normalize(x) for x in value]
 if isinstance(value,str):
  import re
  return re.sub(r'elden-debug-session=[0-9a-f]+','elden-debug-session=SESSION',value).replace('CPU8 protected','CPU2 protected').replace('cpus=8','cpus=2')
 return value
assert normalize(before)==normalize(after),'Unexpected menu/boot-arg drift'
assert sha(source/'kernel.macho')==sha(target/'kernel.macho')
proof={'kernel_sha256':sha(target/'kernel.macho'),'qualification_copies':copied,'toml_diff_only':['title CPU2 to CPU8','cpus=2 to cpus=8','fresh session'],'same_kernel':True,'same_other_args':True}
(target/'same-kernel-comparison.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps(proof,indent=2))
