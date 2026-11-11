"""Build only a guarded IconServices userspace experiment. Never deploy it."""
from pathlib import Path
import hashlib,json,subprocess
r=Path.cwd();assert r==Path('/Users/ciscohe/Desktop/UniXNU_elden')
out=r/'out/icon-cpu-20261111';out.mkdir(exist_ok=True)
src=r/'package/EldenIconCpuMode.c';sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
dest=out/'EldenIconCpuMode.dylib'
cmd=['xcrun','--sdk','iphoneos','clang','-arch','arm64','-miphoneos-version-min=17.0','-isysroot',sdk,'-dynamiclib','-O2','-Wall','-Wextra','-Werror','-fvisibility=hidden','-Wl,-install_name,/private/var/elden/EldenIconCpuMode.dylib','-lobjc',str(src),'-o',str(dest)]
subprocess.run(cmd,check=True)
subprocess.run(['codesign','--force','--sign','-',str(dest)],check=True)
subprocess.run(['codesign','--verify','--strict',str(dest)],check=True)
load=subprocess.check_output(['otool','-l',str(dest)],text=True);(out/'load-commands.txt').write_text(load)
imports=subprocess.check_output(['nm','-u',str(dest)],text=True);(out/'imports.txt').write_text(imports)
assert '_method_setImplementation' in imports and '_objc_getClass' in imports and '_dlopen' in imports
assert 'LC_BUILD_VERSION' in load and 'platform 2' in load
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(source_sha256=sha(src),dylib_sha256=sha(dest),bytes=dest.stat().st_size,architecture='arm64',minimum_ios='17.0',ad_hoc_signature_verified=True,device_deployed=False,hardware_tested=False,launchd_integration=False,mode=0,scope='Exact21H461 IconServices/ISCompositor getter, iconservicesagent process and explicit environment opt-in only',command=cmd)
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
