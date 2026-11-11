"""Generate a complete fresh CPU8 XNU build and its matching compile records."""
from pathlib import Path
import json,os,subprocess,time,sys
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-full-20261111';tree=p/'tree'
# Correct the reusable preparation record to serialize build keys only.
prep=p/'prepare-environment.py';text=prep.read_text()
old="(python_output:=root/'research/cpu-full-20261111/environment.json').write_text(json.dumps(env,indent=2))"
if old in text:
    text=text.replace(old,"selected={k:v for k,v in env.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}\n(root/'research/cpu-full-20261111/environment.json').write_text(json.dumps(selected,indent=2))")
    prep.write_text(text)
selected=json.loads((p/'environment.json').read_text());env=dict(os.environ,**selected)
env['PATH']=str(r/'.venv/bin')+':'+str(r/'.cache/dtc-src/bin')+':'+env['PATH']
env['XNU_BOOTSTRAP']='1';env['JOBS']='4'
start=time.monotonic();print('Starting fresh CPU8 XNU bootstrap in isolated tree.',flush=True)
with (p/'bootstrap.log').open('w') as log:
    result=subprocess.run(['bash',str(tree/'scripts/build-xnu-4k-baseline.sh')],cwd=tree,env=env,stdout=log,stderr=subprocess.STDOUT)
record={'returncode':result.returncode,'elapsed_seconds':time.monotonic()-start,'bootstrap_only':True,'device_operation':False}
(p/'bootstrap-result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record),flush=True)
sys.exit(result.returncode)
