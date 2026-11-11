"""Build the status-reporting library without replacing the v62-v64 artifacts."""
from pathlib import Path
import hashlib,json,subprocess
r=Path.cwd();assert r==Path('/Users/ciscohe/Desktop/UniXNU_elden')
d=r/'out/icon-dependency-20261111';d.mkdir(exist_ok=True)
src=r/'package/EldenIconCpuMode.c';paths=r/'package/EldenIconCpuPaths.h'
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
name='/System/Library/CoreServices/EldenIconCpuMode.dylib'
dest=d/'EldenIconCpuMode.dylib'
cmd=['xcrun','--sdk','iphoneos','clang','-arch','arm64','-miphoneos-version-min=17.0','-isysroot',sdk,'-dynamiclib','-O2','-Wall','-Wextra','-Werror','-fvisibility=hidden','-Wl,-install_name,'+name,'-lobjc',str(src),'-o',str(dest)]
subprocess.run(cmd,check=True);subprocess.run(['codesign','--force','--sign','-',str(dest)],check=True);subprocess.run(['codesign','--verify','--strict',str(dest)],check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(source_sha256=sha(src),paths_sha256=sha(paths),dylib_sha256=sha(dest),bytes=dest.stat().st_size,architecture='arm64',minimum_ios='17.0',install_name=name,ad_hoc_signature_verified=True,mode=0,hardware_tested=False)
(d/'library-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
