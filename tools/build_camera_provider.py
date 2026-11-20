"""Build/sign real-frame OS17 CMIO camera daemons; no installation/cache edit."""
from pathlib import Path
import argparse,hashlib,json,plistlib,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('source','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);src=a.source.resolve(strict=True);record=json.loads((src/'source.json').read_text())
 for n,d in record['sources'].items():
  if sha(src/n)!=d:raise ValueError('Exact source changed')
 def run(args,label):
  r=subprocess.run(list(map(str,args)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(label+' failed; original log retained')
  return r.stdout.decode()
 ios=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'ios-sdk').strip();mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'mac-sdk').strip()
 headers=out/'headers/CoreMediaIO';headers.mkdir(parents=True)
 for f in (Path(mac)/'System/Library/Frameworks/CoreMediaIO.framework/Headers').glob('*.h'):shutil.copyfile(f,headers/f.name)
 for n in ('CameraClient.c','CameraClient.h','CameraProvider.m'):
  if not (src/n).is_file():raise ValueError('Prepared user camera source missing')
 ent=out/'Entitlements.plist';ent.write_bytes(plistlib.dumps({'com.apple.private.security.no-container':True,
   'com.apple.security.iokit-user-client-class':['EldenCameraClient','IOSurfaceRootUserClient'],'com.apple.private.cmio.extension.configuration':True}))
 stream='com.apple.cmio.registerassistantservice.system-extensions.matching'
 binaries={};jobs={}
 for sensor,name in ((0,'rear'),(1,'front')):
  binary_dir=out/name/'bin';binary_dir.mkdir(parents=True);executable=binary_dir/'eldencamerad';info=out/(name+'-embedded-info.plist');identity='org.unixnu.elden.camera.'+name
  info.write_bytes(plistlib.dumps({'CFBundleIdentifier':identity,'CFBundleExecutable':'eldencamerad','CFBundleName':'EldenCamera',
   'CFBundleInfoDictionaryVersion':'6.0','CFBundlePackageType':'SYSX','CFBundleShortVersionString':'1.0','CFBundleVersion':'1',
   'CFBundleSupportedPlatforms':['iPhoneOS'],'CMIOExtension':{'CMIOExtensionMachServiceName':identity},
   'NSCameraUseExternalDeviceType':True,'NSCameraUsageDescription':'Y700 '+name+' camera preview.'}))
  cmd=['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios17.0','-isysroot',ios,'-I',headers.parent,'-I',src,
   '-fobjc-arc','-O2','-Wall','-Wextra','-Werror','-fstack-protector-strong','-framework','Foundation','-framework','CoreMedia','-framework','CoreVideo',
   '-framework','IOKit','-framework','CoreMediaIO','-Wl,-sectcreate,__TEXT,__info_plist,'+str(info),src/'CameraProvider.m',src/'CameraClient.c',src/'Raw10.c','-o',executable]
  run(cmd,name+'-provider-build')
  run(['codesign','--force','--sign','-','--pagesize','4096','--identifier',identity,'--entitlements',ent,executable],name+'-sign')
  run(['codesign','--verify','--strict',executable],name+'-verify');r=subprocess.run(['codesign','-d','--verbose=4',executable],capture_output=True,text=True,check=True)
  (out/(name+'-signature.log')).write_text(r.stderr);cd=re.search(r'^CDHash=([a-f0-9]{40})$',r.stderr,re.M)
  if not cd or not re.search(r'^Page size=4096$',r.stderr,re.M):raise ValueError('Signed identity/page invalid')
  binaries[name]={'sha256':sha(executable),'cdhash':cd[1],'compile_command':list(map(str,cmd)),'sensor':sensor,'identity':identity}
  jobs[identity]={'Label':identity,'ProgramArguments':['/private/var/elden/camera/'+name+'/eldencamerad',str(sensor)],
   'MachServices':{identity:True},'LaunchEvents':{stream:{identity:{'CMIOExtensionBundleIdentifier':identity}}},
   'UserName':'root','ProcessType':'Interactive','RunAtLoad':False,'ThrottleInterval':5}
 (out/'job-template.plist').write_bytes(plistlib.dumps(jobs))
 result={'passed':True,'binaries':binaries,'source_record_sha256':sha(src/'source.json'),'jobs':jobs,
  'native_ticket_RAW10_actual_frames':True,'no_test_pattern':True,'TCC_unchanged':True,'job_cache_installed':False,'trust_installed':False,'hardware':False}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS signed OS17 CMIO real-frame provider; no job/cache/trust installation')
if __name__=='__main__':main()
