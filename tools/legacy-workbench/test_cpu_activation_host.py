"""Run actual deferred methods under sanitizers; no shell argument expansion."""
from pathlib import Path
import hashlib,json,subprocess
r=Path('/mnt/d/Desktop/Git/Patch/Y700-Elden');p=r/'research/cpu-activation-20261111'
assert p.is_dir()
cases={'enabled':['-DELDEN_ARMCPU_DEFERRED=1'],
       'absent':['-DELDEN_ARMCPU_DEFERRED=1','-DMISSING_HOOK=1'],
       'disabled':['-DELDEN_ARMCPU_DEFERRED=0']}
report={'source_sha256':hashlib.sha256((p/'test-deferred.cpp').read_bytes()).hexdigest(),'cases':{},'device_test':False}
for name,flags in cases.items():
    cmd=['clang++','-std=c++17','-pthread','-O1','-g','-fsanitize=address,undefined',
         '-fno-omit-frame-pointer','-fno-pie','-no-pie',*flags,str(p/'test-deferred.cpp'),'-o',str(p/('test-'+name))]
    run=subprocess.run(cmd,capture_output=True,text=True)
    (p/('compile-'+name+'.txt')).write_text(run.stdout+run.stderr)
    assert run.returncode==0,(name,'compile',run.stderr)
    result=subprocess.run([str(p/('test-'+name))],capture_output=True,text=True)
    (p/('result-'+name+'.txt')).write_text(result.stdout+result.stderr)
    report['cases'][name]={'command':cmd,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr}
    (p/'host.json').write_text(json.dumps(report,indent=2)+'\n')
    assert result.returncode==0,(name,result.stdout,result.stderr)
print(json.dumps({name:row['returncode'] for name,row in report['cases'].items()}))
