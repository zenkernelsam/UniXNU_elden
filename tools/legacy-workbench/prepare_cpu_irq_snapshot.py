"""Add bounded read-only GIC evidence to the known guarded two-core test."""
from pathlib import Path
import argparse,importlib.util,json,hashlib,zipfile
parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,choices=[84],default=84);version=parser.parse_args().version
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation');old=r/'research/cpu-guard-20261111';p=r/'research/cpu-mmio-read-20261111';p.mkdir(exist_ok=False)
source=(old/'EldenSdamRecovery.inc').read_text();before=(old/'SmpGuard.inc').read_text();after=(d/'SmpGuard.inc').read_text();assert source.count(before)==1
spec=importlib.util.spec_from_file_location('deferred',d/'Deferred.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
cpu=module.apply((r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes());assert hashlib.sha256(cpu).hexdigest()=='f063df6499bfbc1e7e3500b3eda416c0c9dcc7472bbd4bac026687efe7e654e2'
files={'ArmCpu.cpp':cpu,'EldenSdamRecovery.inc':source.replace(before,after).encode(),
       'SmpGuard.inc':after.encode(),'GuardPolicy.h':(old/'GuardPolicy.h').read_bytes(),'Deferred.py':(d/'Deferred.py').read_bytes()}
manifest={'files':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()},'parent_source_manifest_sha256':hashlib.sha256((old/'source.json').read_bytes()).hexdigest(),'hardware_writes_added':False,'plain_mmio_ldr':True,'scope':'v83 read-only snapshots with read32 forced to plain LDR;no address/value changes','hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
