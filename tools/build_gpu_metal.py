"""Qualify and build the A830 iOS17 Metal/plugin front end in an isolated tree.

The syntax phase may run while Rust compiles. Full build requires a completed
bridge archive and an audited Turnip producer tree. No plugin is installed.
"""
from pathlib import Path
import argparse, hashlib, json, plistlib, re, shutil, struct, subprocess, sys

def sha(p):
 with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def ios17_image(path,kind):
 b=path.read_bytes()
 if len(b)<32 or struct.unpack_from('<4I',b)!=(0xfeedfacf,0x100000c,0,kind):raise ValueError('Wrong Mach-O architecture/kind: '+str(path))
 at=32;version=None;fixups=[]
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,n=struct.unpack_from('<II',b,at)
  if n<8 or at+n>len(b):raise ValueError('Malformed Mach-O command')
  if cmd==0x32:version=struct.unpack_from('<II',b,at+8)
  if cmd in (0x80000034,0x80000022,0x22):fixups.append(cmd)
  at+=n
 if version!=(2,0x110000):raise ValueError('Expected iOS17 minimum version: '+str(path))
 return fixups
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--syntax-only',action='store_true')
 p.add_argument('--turnip-tree',type=Path);a=p.parse_args();tree=a.tree.resolve();out=a.output.resolve()
 out.mkdir(parents=True,exist_ok=False);commands=[]
 def run(cmd,name):
  commands.append([str(x) for x in cmd])
  with (out/(name+'.log')).open('xb') as f:r=subprocess.run([str(x) for x in cmd],stdout=f,stderr=subprocess.STDOUT)
  if r.returncode:raise RuntimeError(name+' failed; see '+str(out))
 run([sys.executable,tree/'scripts/gen-turnip-smoke-shaders.py'],'shaders')
 if (tree/'build/turnip-smoke/TurnipReplay.h').exists():raise ValueError('A830 must not consume an A740 replay header')
 sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
 includes=[tree/'drivers/Adreno740',tree/'drivers/GopFramebuffer',tree/'kits/AdrenoPM4',tree/'.cache/mesa-work/include',tree/'build/turnip-smoke']
 flags=['xcrun','clang++','-target','arm64-apple-ios17.0','-isysroot',sdk,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
        '-Wno-deprecated-declarations','-fobjc-arc','-DNATIVE_ADRENO_SP_CORES=6',*[v for d in includes for v in ('-I',str(d))]]
 sources=[tree/'drivers/AdrenoMetal'/n for n in ('NativeAdrenoMetalPlugin.mm','NativeBridgeMetalFrontend.mm')]
 for src in sources:run([*flags,'-fsyntax-only','-x','objective-c++',src],src.stem+'-syntax')
 report={'syntax_passed':True,'shader_processors':6,'target':'arm64-apple-ios17.0','replay':False,
         'sources':{str(p.relative_to(tree)):sha(p) for p in sources},'full_bundle':False,'device_access':False,'commands':commands}
 (out/'syntax.json').write_text(json.dumps(report,indent=2)+'\n')
 if a.syntax_only:print('PASS: A830 Metal frontend syntax and shaders');return
 if not a.turnip_tree:raise ValueError('Full build requires --turnip-tree')
 producer=a.turnip_tree.resolve()
 if sha(producer/'drivers/Adreno740/NativeAdrenoABI.h')!=sha(tree/'drivers/Adreno740/NativeAdrenoABI.h'):
  raise ValueError('Turnip producer and Metal consumer wire headers differ')
 turnip=producer/'build/mesa-tu-ios/src/freedreno/vulkan/libvulkan_freedreno.dylib'
 ios17_image(turnip,6)
 bridge=tree/'build/adreno-metal-bridge/ios/libadreno_metal_bridge.a'
 if not bridge.is_file():raise ValueError('Completed Rust bridge archive required')
 with bridge.open('rb') as stream:
  if stream.read(8)!=b'!<arch>\n':raise ValueError('Rust bridge is not an archive')
 bundle=out/'NativeAdrenoMetal.bundle';bundle.mkdir();exe=bundle/'NativeAdrenoMetal'
 pm4=out/'AdrenoPM4.o'
 run(['xcrun','clang','-target','arm64-apple-ios17.0','-isysroot',sdk,'-O2','-g','-Wall','-Wextra','-Werror',
      '-fstack-protector-strong','-std=c11','-c',tree/'kits/AdrenoPM4/AdrenoPM4.c','-o',pm4],'pm4')
 run([*flags,'-fstack-protector-strong','-bundle','-x','objective-c++',*sources,'-x','none',pm4,bridge,
      '-Wl,-dead_strip','-lobjc','-framework','Foundation','-framework','Metal','-framework','IOSurface','-o',exe],'plugin-link')
 ios17_image(exe,8)
 run(['strip','-x',exe],'plugin-strip')
 # Keep any dsym outside the bundle. Never recursively erase prior outputs.
 generated=bundle/'NativeAdrenoMetal.dSYM'
 if generated.exists():shutil.move(str(generated),str(out/generated.name))
 copied=bundle/'libvulkan_freedreno.dylib';shutil.copyfile(turnip,copied)
 run(['strip','-x',copied],'turnip-strip')
 fixups=ios17_image(copied,6)
 if 0x80000034 in fixups:
  run([sys.executable,tree/'scripts/macho-self-bind-weak-imports.py',copied],'turnip-weak-binding')
  weak_binding='upstream chained-import self-binding applied'
 elif any(x in fixups for x in (0x80000022,0x22)):
  # Legacy dyld bind opcodes are valid on iOS17. The upstream optimization
  # edits chained-import ordinals only; don't reinterpret legacy bytes or
  # change their existing lookup semantics to pretend that pass ran.
  weak_binding='legacy dyld info preserved; chained-import optimization not applicable'
  (out/'turnip-weak-binding.log').write_text(weak_binding+'\n')
 else:raise ValueError('Turnip lacks a recognized dynamic-binding representation')
 (bundle/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'org.blu-sharky.NativeAdrenoMetal',
  'CFBundleExecutable':'NativeAdrenoMetal','CFBundlePackageType':'BNDL','CFBundleVersion':'1','CFBundleShortVersionString':'1',
  'NSPrincipalClass':'NativeAdrenoMetalDevice','MinimumOSVersion':'17.0'}))
 def sign(path,identifier,extra=()):
  run(['codesign','--force','--sign','-','--pagesize','4096','--identifier',identifier,*extra,path],identifier+'-sign')
  run(['codesign','--verify','--strict',path],identifier+'-verify')
  p=subprocess.run(['codesign','--display','--verbose=4',str(path)],capture_output=True,text=True,check=True)
  match=re.search(r'^CDHash=([a-f0-9]{40})$',p.stderr,re.M)
  if not match or not re.search(r'^Page size=4096$',p.stderr,re.M):raise ValueError('Wrong signature page size')
  return match[1]
 turnip_cdhash=sign(copied,'org.blu-sharky.turnip')
 plugin_cdhash=sign(bundle,'org.blu-sharky.NativeAdrenoMetal')
 daemon=out/'adrenoprobed'
 run([*flags,'-fstack-protector-strong',tree/'drivers/AdrenoMetal/NativeAdrenoProbeDaemon.cpp','-x','objective-c++',
      tree/'drivers/AdrenoMetal/NativeOsLogExport.mm','-x','none',pm4,'-framework','Foundation','-lz','-o',daemon],'probe-link')
 ios17_image(daemon,2)
 ent={'com.apple.private.security.no-container':True,'com.apple.security.iokit-user-client-class':['AGXDeviceUserClient'],
      'com.apple.private.logging.admin':True,'com.apple.private.logging.diagnostic':True,'com.apple.private.logging.stream':True}
 ep=out/'adrenoprobed-entitlements.plist';ep.write_bytes(plistlib.dumps(ent))
 daemon_cdhash=sign(daemon,'org.blu-sharky.adrenoprobed',['--entitlements',ep])
 signed=plistlib.loads(subprocess.check_output(['codesign','-d','--entitlements',':-',str(daemon)],stderr=subprocess.DEVNULL))
 if signed!=ent:raise ValueError('Probe entitlement round trip differs')
 (out/'org.blu-sharky.adrenoprobed.plist').write_bytes(plistlib.dumps({'Label':'org.blu-sharky.adrenoprobed',
  'ProgramArguments':['/private/var/ngpu/bin/adrenoprobed'],'RunAtLoad':True,'KeepAlive':False,'ThrottleInterval':30,'UserName':'root'}))
 report.update(full_bundle=True,bridge_sha256=sha(bridge),turnip_input_sha256=sha(turnip),
  wire_header_sha256=sha(tree/'drivers/Adreno740/NativeAdrenoABI.h'),
  artifacts={str(p.relative_to(out)):{'sha256':sha(p),'bytes':p.stat().st_size,
             'dependencies':subprocess.check_output(['otool','-L',str(p)],text=True).splitlines()[1:]}
             for p in (exe,copied,daemon)},
  cdhashes={'plugin':plugin_cdhash,'turnip':turnip_cdhash,'probe':daemon_cdhash},system_job_overrides=False,
  weak_import_binding=weak_binding)
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS: A830 iOS17 Metal bundle and probe linked and 4K-signed; not installed')
if __name__=='__main__':main()
