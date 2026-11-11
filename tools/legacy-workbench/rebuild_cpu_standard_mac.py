"""Rebuild CPU8 kernel dependencies only inside the private tree."""
from pathlib import Path
import json,os,subprocess,time
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-full-20261111';tree=p/'tree';old=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
env=dict(os.environ,**json.loads((p/'environment.json').read_text()))
for k,v in list(env.items()):
    if k.startswith(('XNU_','ELDEN_')):env[k]=v.replace(str(old),str(tree))
env['PATH']=str(r/'.venv/bin')+':'+str(r/'.cache/dtc-src/bin')+':'+env['PATH'];env['JOBS']='4';env['XNU_BOOTSTRAP']='0'
extra=json.loads((p/'external-build.json').read_text())
for group,row in extra['groups'].items():
    original=json.loads(Path(row['original_build_record']).read_text())['archive']
    env['XNU_EXTRA_LD_ARCHIVES']=env['XNU_EXTRA_LD_ARCHIVES'].replace(original,row['archive'])
for path in (tree/'.cache/xnu-work',tree/'.cache/libpthread-work'):
    if not path.exists():continue
    assert path.resolve().is_relative_to(tree.resolve()) and not path.is_symlink()
    top=Path(subprocess.check_output(['git','-C',str(path),'rev-parse','--show-toplevel'],text=True).strip())
    assert top.resolve()==path.resolve()
steps=[('corecrypto',['scripts/build-corecrypto-14.py','--namespace']),
       ('pthread',['scripts/build-libpthread-kernel.py']),
       ('mockfs',['scripts/build-mockfs-fixture.py']),
       ('armdt',['scripts/build-arm-driver.py','ArmDTPlatform']),
       ('armcpu',['scripts/build-arm-driver.py','ArmCpu']),
       ('aes',['scripts/build-arm-driver.py','SoftwareAES']),
       ('aes-bindings',['scripts/build-software-aes-bindings.py']),
       ('ufs',['scripts/build-sheng-ufs-probe.py']),
       ('ramblock',['scripts/build-elden-ram-block.py']),
       ('display',['scripts/build-elden-gop.py'])]
status=[]
for name,args in steps:
    before=time.monotonic();print('Building CPU8 dependency '+name,flush=True)
    with (p/(name+'-build.log')).open('w') as log:
        result=subprocess.run([r/'.venv/bin/python',*args],cwd=tree,env=env,stdout=log,stderr=subprocess.STDOUT)
    status.append({'step':name,'returncode':result.returncode,'elapsed_seconds':time.monotonic()-before})
    (p/'standard-build.json').write_text(json.dumps(status,indent=2)+'\n')
    if result.returncode:raise SystemExit(name+' failed; see its isolated log')
selected={k:v for k,v in env.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}
(p/'final-environment.json').write_text(json.dumps(selected,indent=2)+'\n')
print('CPU8 dependencies built; final link remains.',flush=True)
