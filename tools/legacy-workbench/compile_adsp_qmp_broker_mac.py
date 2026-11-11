"""Compile the changed ADSP shared-QMP guard without overwriting prior archives."""
from pathlib import Path
import hashlib,json,os,subprocess
root=Path('/Users/ciscohe/Desktop/UniXNU_elden')
old=root/'out/adsp-pmic-client-20261108/EldenAdspRuntime'
out=root/'out/wcn-power-20261108/adsp-qmp-broker';out.mkdir(exist_ok=True)
record=json.loads((old/'build.json').read_text())
command=next(list(c) for c in record['commands'] if any(x.endswith('/AdspProbe.cpp') for x in c))
source=root/'drivers/EldenPmicGlink/AdspProbe.cpp'
for i,v in enumerate(command):
    if v.endswith('/AdspProbe.cpp'):command[i]=str(source)
command[command.index('-o')+1]=str(out/'AdspProbe.o')
command+=['-I',str(root/'soc/sm8750/include')]
tree=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
cpo=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
directory=json.loads(cpo.read_text().strip().rstrip(','))['directory']
env=dict(os.environ);env['CCC_OVERRIDE_OPTIONS']='+-Wno-unknown-warning-option'
r=subprocess.run(command,cwd=directory,env=env,capture_output=True,text=True)
(out/'compile.log').write_text(r.stdout+r.stderr)
r.check_returncode()
obj=(out/'AdspProbe.o').read_bytes()
result={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'object_sha256':hashlib.sha256(obj).hexdigest(),'object_bytes':len(obj),
        'native_ios17_compile':True,'hardware_tested':False,'kernel_includes_this_object':False}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
