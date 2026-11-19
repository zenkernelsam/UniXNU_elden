"""Build/sign the OS17 haptics-only audio server plug-in, never install it."""
from pathlib import Path
import argparse,hashlib,json,plistlib,re,struct,subprocess
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['source','output']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=json.loads((a.source/'source.json').read_text())
    for name,digest in spec['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Source identity differs')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    ios=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'ios-sdk').strip();mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'mac-sdk').strip()
    headers=out/'headers/CoreAudio';headers.mkdir(parents=True)
    for name in ['AudioServerPlugIn.h','AudioHardwareBase.h','AudioHardware.h','AudioHardwareDeprecated.h']:
        original=Path(mac)/'System/Library/Frameworks/CoreAudio.framework/Headers'/name
        if not original.is_file():raise ValueError('Required CoreAudio header absent')
        (headers/name).symlink_to(original)
    bundle=out/'EldenActuator.driver';bundle.mkdir();exe=bundle/'EldenActuator';info=plistlib.loads((a.source/'Info.plist').read_bytes())
    if info['CFBundleIdentifier']!='org.unixnu.elden.actuator' or list(info['CFPlugInFactories'].values())!=['EldenActuator_Create']:raise ValueError('Factory metadata differs')
    cmd=['xcrun','clang','-target','arm64-apple-ios17.0','-isysroot',ios,'-std=c11','-O2','-g0','-Wall','-Wextra','-Werror',
         '-I',headers.parent,'-I',a.source,'-bundle','-fvisibility=hidden','-framework','CoreFoundation','-framework','IOKit',a.source/'PlugIn.c',a.source/'Ring.c','-o',exe]
    run(cmd,'compile');shutil_info=bundle/'Info.plist';shutil_info.write_bytes((a.source/'Info.plist').read_bytes())
    (bundle/'LICENSE').write_bytes((a.source/'LICENSE').read_bytes())
    raw=exe.read_bytes()
    if struct.unpack_from('<4I',raw)!=(0xfeedfacf,0x100000c,0,8):raise ValueError('Not ordinary ARM64 MH_BUNDLE')
    at=32;version=None
    for _ in range(struct.unpack_from('<I',raw,16)[0]):
        tag,size=struct.unpack_from('<II',raw,at)
        if tag==0x32:version=struct.unpack_from('<II',raw,at+8)
        at+=size
    if version!=(2,0x110000):raise ValueError('Unexpected OS ABI target')
    symbols=run(['xcrun','nm','-gU',exe],'exports')
    if '_EldenActuator_Create' not in symbols:raise ValueError('Factory export absent')
    run(['codesign','--force','--sign','-','--pagesize','4096','--identifier',info['CFBundleIdentifier'],bundle],'sign')
    run(['codesign','--verify','--strict',bundle],'verify')
    r=subprocess.run(['codesign','-d','--verbose=4',bundle],capture_output=True,text=True,check=True);(out/'signature.log').write_text(r.stderr)
    match=re.search(r'^CDHash=([a-f0-9]{40})$',r.stderr,re.M)
    if not match or not re.search(r'^Page size=4096$',r.stderr,re.M):raise ValueError('Signature identity/page differs')
    report={'compiled':True,'signed_4k':True,'bundle_identifier':info['CFBundleIdentifier'],'factory':'EldenActuator_Create','cdhash':match[1],
        'sha256':sha(exe),'source_record_sha256':sha(a.source/'source.json'),'compile_command':list(map(str,cmd)),
        'only_haptics_no_speaker_or_input':True,'installed':False,'native_service_connected':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: signed ARM64/iOS17 Actuator-only bundle; not installed or connected to native service')
if __name__=='__main__':main()
