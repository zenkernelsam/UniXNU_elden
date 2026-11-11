from pathlib import Path
import argparse,hashlib,json,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[90,91],default=90);v=a.parse_args().version
src=r/'research/graphic-variant-cpu-20261111'/('library-input' if v==90 else f'library-input-v{v}');out=r/f'out/icon-dependency-20261111/v{v}';out.mkdir(parents=True,exist_ok=True)
assert not (out/'library-build.json').exists()
sources=[src/n for n in ('EldenIconCpuMode.c','EldenGraphicVariantHook.c','EldenGraphicVariantCpu.c')]
library=out/'EldenIconCpuMode.dylib';install='/System/Library/CoreServices/EldenIconCpuMode.dylib'
cmd=['xcrun','--sdk','iphoneos','clang','-arch','arm64','-miphoneos-version-min=17.0','-dynamiclib','-O2','-Wall','-Wextra','-Werror','-fvisibility=hidden','-DELDEN_GRAPHIC_VARIANT_CPU=1','-Wl,-install_name,'+install,'-lobjc','-framework','CoreGraphics','-framework','CoreFoundation',*[str(p) for p in sources],'-o',str(library)]
subprocess.run(cmd,check=True);subprocess.run(['codesign','--force','--sign','-',library],check=True);subprocess.run(['codesign','--verify','--strict',library],check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'source_sha256':sha(src/'EldenIconCpuMode.c'),'paths_sha256':sha(src/'EldenIconCpuPaths.h'),'extra_sources':{n:sha(src/n) for n in ('EldenGraphicVariantHook.c','EldenGraphicVariantCpu.c','EldenGraphicVariantCpu.h')},'dylib_sha256':sha(library),'bytes':library.stat().st_size,'architecture':'arm64','minimum_ios':'17.0','install_name':install,'ad_hoc_signature_verified':True,'mode':0,'graphic_variant_cpu':True,'coreui_uuid':'d62914c6c4dc3c00a175b515a6b901a3','target_selector':'_createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:','command':cmd,'hardware_tested':False}
(out/'library-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
