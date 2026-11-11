"""Create a private APFS-cloned build tree; preserve the verified v77 checkout."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
old=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
p=r/'research/cpu-full-20261111';tree=p/'tree';assert not p.exists()
with zipfile.ZipFile(r/'out/basic-bundle-source-upload-v77.zip') as z:
    m=json.loads(z.read('manifest.json'))
    for n,h in m.items():assert hashlib.sha256((r/n).read_bytes()).hexdigest()==h,n
baseline=json.loads((r/'research/cpu-affinity-20261111/manifest.json').read_text())
for n,h in baseline.items():assert hashlib.sha256((old/'.cache/xnu-work'/n).read_bytes()).hexdigest()==h,n
p.mkdir()
subprocess.run(['cp','-cR',str(old),str(tree)],check=True)
x=tree/'.cache/xnu-work'
top=Path(subprocess.check_output(['git','-C',str(x),'rev-parse','--show-toplevel'],text=True).strip()).resolve()
gitdir=Path(subprocess.check_output(['git','-C',str(x),'rev-parse','--absolute-git-dir'],text=True).strip()).resolve()
common=subprocess.check_output(['git','-C',str(x),'rev-parse','--git-common-dir'],text=True).strip()
assert top==x.resolve() and gitdir.is_relative_to(x.resolve())
assert (x/common).resolve().is_relative_to(x.resolve()),common
# Use a genuine separate build directory. Old records remain available only for
# remapping recipes; no old XNU object is eligible for the new link.
old_build=tree/'build/xnu';saved=tree/'build/xnu-v77-records'
assert old_build.resolve().is_relative_to(tree.resolve()) and not saved.exists()
old_build.rename(saved)
patch=r/'research/cpu-affinity-20261111/upstream-sm8750-smp.patch'
assert hashlib.sha256(patch.read_bytes()).hexdigest()=='0f039b8ed699d4b2e6de3d4d15b48567510ffb9da6660d8d8cdac9b28e52f8bf'
subprocess.run(['git','-C',str(x),'apply','--check',str(patch)],check=True)
subprocess.run(['git','-C',str(x),'apply',str(patch)],check=True)
cpu=tree/'drivers/ArmCpu';assert cpu.is_symlink()
target=cpu.resolve();assert target==r/'third_party/UniXNU/drivers/ArmCpu'
assert hashlib.sha256((target/'ArmCpu.cpp').read_bytes()).hexdigest()=='3c87b3bd617be5582a6a24ff797c359497ee9feaa702ee883ecee4270db21c9f'
cpu.unlink();shutil.copytree(target,cpu,symlinks=True,ignore=shutil.ignore_patterns('.git'))
new=r/'research/cpu-affinity-20261111/candidate/ArmCpu.cpp'
assert hashlib.sha256(new.read_bytes()).hexdigest()=='815868520794ce7d7230adac373928a107a2bae6d427cc4513daa533919896dc'
shutil.copyfile(new,cpu/'ArmCpu.cpp')
# The constructor writes clone paths only; its immutable inputs still come from
# this project's qualified v77 source and archives.
script=(r/'out/build_basic_bundle-v77.py').read_text()
script=script.replace("tree=base/'tree'","tree=root/'research/cpu-full-20261111/tree'")
script=script.replace("out=root/'out/startup-basic-bundle-v77-20261108'","out=root/'research/cpu-full-20261111/out'")
cut=script.index("log=out/'build.log'")
prefix=script[:cut]
(p/'prepare-environment.py').write_text(prefix+"\nselected={k:v for k,v in env.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}\n(root/'research/cpu-full-20261111/environment.json').write_text(json.dumps(selected,indent=2))\n")
subprocess.run([r/'.venv/bin/python',p/'prepare-environment.py'],cwd=r,check=True)
# Do not export credentials from the process environment. Only the build-driving
# keys below are retained in the project record.
environment=json.loads((p/'environment.json').read_text())
selected={k:v for k,v in environment.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}
(p/'environment.json').write_text(json.dumps(selected,indent=2)+'\n')
record={'source_tree':str(old),'isolated_tree':str(tree),'baseline_sources':baseline,
        'capacity_patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),
        'armcpu_source_sha256':hashlib.sha256((cpu/'ArmCpu.cpp').read_bytes()).hexdigest(),
        'old_objects_excluded_from_new_objroot':True,'device_operation':False}
(p/'preparation.json').write_text(json.dumps(record,indent=2)+'\n')
print('Isolated CPU tree and clean XNU output prepared; verified tree preserved.')
