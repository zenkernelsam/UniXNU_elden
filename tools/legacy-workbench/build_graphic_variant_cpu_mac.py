from pathlib import Path
import hashlib,json,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r;p=r/'research/graphic-variant-cpu-20261111';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
host=['/usr/bin/clang','-std=c11','-O1','-fsanitize=address,undefined','-framework','CoreGraphics','-framework','CoreFoundation',str(p/'EldenGraphicVariantCpu.c'),str(p/'TestGraphicVariantCpu.c'),'-o',str(p/'test-cpu')]
subprocess.run(host,check=True);test=subprocess.run([p/'test-cpu'],capture_output=True,text=True,check=True)
native=['xcrun','--sdk','iphoneos','clang','-arch','arm64','-miphoneos-version-min=17.0','-O2','-c',str(p/'EldenGraphicVariantCpu.c'),'-o',str(p/'EldenGraphicVariantCpu.ios.o')]
subprocess.run(native,check=True)
report={'host_passed':True,'stdout':test.stdout,'stderr':test.stderr,'host_command':host,'native_command':native,'sources':{n:sha(p/n) for n in ('EldenGraphicVariantCpu.c','EldenGraphicVariantCpu.h','TestGraphicVariantCpu.c')},'native_object_sha256':sha(p/'EldenGraphicVariantCpu.ios.o'),'private_method_hook_installed':False,'hardware_tested':False,'limitations':['extra CI content/shape effects not implemented','8-bit output compatibility','original method ABI/runtime guards still required']}
(p/'core-build.json').write_text(json.dumps(report,indent=2)+'\n');print(test.stdout)
