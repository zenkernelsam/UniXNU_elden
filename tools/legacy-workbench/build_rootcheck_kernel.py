"""Mac runner: isolated root-check configuration, restoring the prior selection."""
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path.cwd()
out = root/'out/rootcheck-20261106'
out.mkdir(parents=True,exist_ok=True)
cli = root/'third_party/UniXNU/unixnu'
env=dict(os.environ)
env['PATH']=str(root/'.cache/dtc-src/bin')+':'+str(root/'.cache/mtools/bin')+':'+str(root/'.venv/bin')+':'+env['PATH']
env['CCC_OVERRIDE_OPTIONS']='+-Wno-unknown-warning-option'
env['ELDEN_STORAGE_ABI_AUDIT']=str(root/'out/elden-storage-abi/build/storage-abi/interface-audit.json')
env['ELDEN_STORAGE_ABI_CANDIDATE']=str(root/'out/bringup-20261105/kernel-gop.macho')
env['ELDEN_STORAGE_ABI_PLAN']=str(root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_probe/kernel/kernel.json')
for name in ('ELDEN_STORAGE_ABI_AUDIT','ELDEN_STORAGE_ABI_CANDIDATE','ELDEN_STORAGE_ABI_PLAN'):
    if not Path(env[name]).is_file():
        raise SystemExit('missing qualified build input '+name)
status=json.loads(subprocess.check_output([sys.executable,cli,'status','--json','-D',root],env=env,text=True))
previous=status['configuration']
selected=False
result=1
try:
    subprocess.run([sys.executable,cli,'defconfig','elden-ufs_rootcheck_defconfig','-D',root],env=env,check=True)
    selected=True
    with (out/'build.log').open('w') as log:
        child=subprocess.Popen([sys.executable,cli,'build','kernel','-D',root,'-j4'],env=env,stdout=log,stderr=subprocess.STDOUT)
        (out/'job.json').write_text(json.dumps({'pid':child.pid,'configuration':'elden-ufs_rootcheck_defconfig','previous':previous})+'\n')
        result=child.wait()
finally:
    if selected:
        restored=subprocess.run([sys.executable,cli,'defconfig',previous,'-D',root],env=env)
        (out/'result.json').write_text(json.dumps({'build_returncode':result,'restore_returncode':restored.returncode,'previous':previous})+'\n')
print((out/'build.log').read_text()[-3500:])
raise SystemExit(result)
