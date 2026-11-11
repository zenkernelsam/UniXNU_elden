"""Export private CPU build evidence, without modifying build or device inputs."""
from pathlib import Path
import difflib, hashlib, json, zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden'); assert Path.cwd()==r
p=r/'research/cpu-full-20261111'; t=p/'tree'; x=t/'.cache/xnu-work'
prep=r/'research/cpu-affinity-20261111'
manifest=json.loads((prep/'candidate-manifest.json').read_text())
baseline=json.loads((prep/'manifest.json').read_text())
old=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/.cache/xnu-work'
sha=lambda b:hashlib.sha256(b).hexdigest()
report={'candidate_comparison':{},'original_tree_unchanged':{}}
for name, h in baseline.items():
    actual=sha((old/name).read_bytes()); assert actual==h,name
    report['original_tree_unchanged'][name]=actual
for name, h in manifest['files'].items():
    staged=(prep/'candidate'/name).read_bytes(); assert sha(staged)==h,name
    src=t/'drivers/ArmCpu/ArmCpu.cpp' if name=='ArmCpu.cpp' else x/name
    built=src.read_bytes(); a=staged.decode().replace('\r\n','\n'); b=built.decode().replace('\r\n','\n')
    row={'staged_sha256':h,'built_sha256':sha(built),'identical_bytes':staged==built,'identical_lf_text':a==b}
    if a!=b:row['diff']=''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='staged/'+name,tofile='built/'+name))
    report['candidate_comparison'][name]=row
(p/'audit/source-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
names=['result.json','preparation.json','external-build.json','standard-build.json','bootstrap-result.json','final-environment.json',
       'audit/layout.json','audit/layout.s','audit/source-comparison.json','ArmCpu-scalar.asm',
       'out/kernel.json','out/kernel.macho','final-link.log','prelink.log','bootstrap.log','final-link-aes-missing.log']
with zipfile.ZipFile(p/'evidence.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in names:
        f=p/name
        if f.is_file():z.write(f,name)
    for name in manifest['files']:
        f=t/'drivers/ArmCpu/ArmCpu.cpp' if name=='ArmCpu.cpp' else x/name
        z.write(f,'sources/'+name)
    for name in ['osfmk/arm/cpu_data_internal.h','osfmk/arm/machine_routines.h','osfmk/kern/sched.h']:
        z.write(x/name,'sources/'+name)
    z.write(t/'build/armcpu/compile-command.json','audit/armcpu-command.json')
print(json.dumps({'zip_bytes':(p/'evidence.zip').stat().st_size,'sources':report['candidate_comparison']},indent=2))
