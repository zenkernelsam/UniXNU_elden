"""Stage proven SM8750 topology policy in an inactive research tree only."""
from pathlib import Path
import difflib,hashlib,json,shutil,subprocess
r=Path(__file__).resolve().parents[1];p=r/'research/cpu-affinity-20261111';shadow=p/'candidate'
source=r/'research/UniXNU-sm8750-common-pagani/patches/xnu/sm8750-smp.patch'
patch=p/'upstream-sm8750-smp.patch';patch.write_text(source.read_text(),encoding='utf-8',newline='\n')
if not shadow.exists():shutil.copytree(p/'baseline',shadow)
for f in (p/'baseline').rglob('*'):
    if f.is_file():assert (shadow/f.relative_to(p/'baseline')).read_bytes()==f.read_bytes(),'Candidate already modified; review before restaging'
repo=Path(subprocess.check_output(['git','-C',str(r),'rev-parse','--show-toplevel'],text=True).strip())
cmd=['git','-C',str(repo),'apply','--directory='+shadow.relative_to(repo).as_posix()]
subprocess.run(cmd+['--check',str(patch)],check=True);subprocess.run(cmd+[str(patch)],check=True)
a=(r/'research/cpu-smp-review-20261110/ArmCpu.cpp').read_text();new=a
old='if ((mpidr & 0xffff) != (affinity & 0xffff))'
assert new.count(old)==1;new=new.replace(old,'if ((mpidr & 0xff00ffffffULL) != (affinity & 0xff00ffffffULL))')
assert new.count('armcpu_smp_running = armcpu_smp_running + 1;')==1
new=new.replace('armcpu_smp_running = armcpu_smp_running + 1;','__atomic_add_fetch (&armcpu_smp_running, 1, __ATOMIC_RELAXED);')
(shadow/'ArmCpu.cpp').write_text(new,encoding='utf-8',newline='\n')
(p/'armcpu-affinity.patch').write_text(''.join(difflib.unified_diff(a.splitlines(True),new.splitlines(True),fromfile='a/ArmCpu.cpp',tofile='b/ArmCpu.cpp')),newline='\n')
notes={'upstream_repo':'UniXNU-sm8750-common-pagani','upstream_revision':'feaa135df476',
 'source_patch_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'lf_patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),
 'device_evidence':'Pagani Build63 reports reg8/online8/Tff/Iff; cross-core stress is unqualified',
 'selected_for_device_build':False,'hardware_tested':False,
 'files':{f.relative_to(shadow).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in shadow.rglob('*') if f.is_file()}}
(p/'candidate-manifest.json').write_text(json.dumps(notes,indent=2)+'\n')
print('Staged upstream capacity/affinity/Clutch policy and minimal ArmCpu delta; active build tree unchanged.')
