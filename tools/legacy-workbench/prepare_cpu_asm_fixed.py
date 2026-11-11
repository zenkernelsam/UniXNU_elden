from pathlib import Path
import hashlib,importlib.util,json,zipfile,re
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation');old=r/'research/cpu-mmio-read-20261111';p=r/'research/cpu-asm-fixed-20261111';p.mkdir(exist_ok=False)
spec=importlib.util.spec_from_file_location('deferred',d/'Deferred.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
base=(r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes();cpu=module.apply(base)
assert (d/'SmpGuard.inc').read_bytes()==(old/'SmpGuard.inc').read_bytes()
pattern=re.compile(r'__asm__\s+volatile\s*\(\s*"((?:[^"\\]|\\.)*)"')
before=[m[1] for m in pattern.finditer((old/'ArmCpu.cpp').read_text()) if ';' in m[1]]
assert before and not any(';' in m[1] for m in pattern.finditer(cpu.decode()))
files={'ArmCpu.cpp':cpu,'EldenSdamRecovery.inc':(old/'EldenSdamRecovery.inc').read_bytes(),**{n:(d/n).read_bytes() for n in ('SmpGuard.inc','GuardPolicy.h','Deferred.py')}}
manifest={'files':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()},'parent_source_manifest_sha256':hashlib.sha256((r/'research/cpu-guard-20261111/source.json').read_bytes()).hexdigest(),'assembly_delimiters_fixed':True,'irq_numbers_changed':False,'register_values_changed':False,'repaired_asm_strings':before,'hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print({'source_sha256':manifest['files']['ArmCpu.cpp'],'repaired_asm_strings':len(before)})
