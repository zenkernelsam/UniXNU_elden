#!/usr/bin/env python3
"""Build the frozen owned v91 IconServices helper on macOS. No OS binary bundled."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from reconstruct_v91 import verify,PROFILE

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if sys.platform!='darwin':raise SystemExit('macOS with the iPhoneOS SDK is required')
    verify();out=a.output.resolve()
    if out.exists():raise SystemExit('Output must be a new directory')
    out.mkdir(parents=True);src=PROFILE/'sources/package'
    names=['EldenIconCpuMode.c','EldenGraphicVariantHook.c','EldenGraphicVariantCpu.c']
    lib=out/'EldenIconCpuMode.dylib'
    cmd=['xcrun','--sdk','iphoneos','clang','-arch','arm64','-miphoneos-version-min=17.0',
         '-dynamiclib','-O2','-Wall','-Wextra','-Werror','-fvisibility=hidden',
         '-DELDEN_GRAPHIC_VARIANT_CPU=1','-Wl,-install_name,/System/Library/CoreServices/EldenIconCpuMode.dylib',
         '-lobjc','-framework','CoreGraphics','-framework','CoreFoundation',*[str(src/n) for n in names],'-o',str(lib)]
    subprocess.run(cmd,check=True);subprocess.run(['codesign','--force','--sign','-',str(lib)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(lib)],check=True)
    h=hashlib.sha256(lib.read_bytes()).hexdigest()
    result={'sha256':h,'original_v91_sha256':'f748e9cf601db43167eaf7736b0a161771b8c01eeb46860820604ccf892ac034',
            'byte_identical':h=='f748e9cf601db43167eaf7736b0a161771b8c01eeb46860820604ccf892ac034',
            'codesign_verified':True,'device_operation':False,
            'compiler':subprocess.check_output(['xcrun','--sdk','iphoneos','clang','--version'],text=True),
            'sdk':subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-version'],text=True).strip()}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
