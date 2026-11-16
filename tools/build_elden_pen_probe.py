"""Build/sign the normal UIKit pen diagnostic; do not install or launch it."""
from pathlib import Path
import argparse,hashlib,json,plistlib,re,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);app=out/'EldenPenProbe.app';app.mkdir()
    exe=app/'EldenPenProbe';source=ROOT/'apps/EldenPenProbe/main.mm'
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
    cmd=['xcrun','clang++','-target','arm64-apple-ios17.0','-isysroot',sdk,'-std=c++17','-O2','-fobjc-arc',
         '-Wall','-Wextra','-Werror','-fstack-protector-strong',str(source),'-framework','UIKit','-framework','Foundation','-framework','CoreGraphics','-o',str(exe)]
    r=subprocess.run(cmd,capture_output=True);(out/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    info={'CFBundleIdentifier':'org.unixnu.elden.penprobe','CFBundleExecutable':'EldenPenProbe','CFBundleName':'Elden Pen Probe',
          'CFBundlePackageType':'APPL','CFBundleVersion':'1','CFBundleShortVersionString':'1.0','MinimumOSVersion':'17.0',
          'CFBundleSupportedPlatforms':['iPhoneOS'],'UIDeviceFamily':[2],'LSRequiresIPhoneOS':True,'UILaunchScreen':{},
          'UISupportedInterfaceOrientations':['UIInterfaceOrientationPortrait','UIInterfaceOrientationPortraitUpsideDown','UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
          'UIApplicationSceneManifest':{'UIApplicationSupportsMultipleScenes':False}}
    (app/'Info.plist').write_bytes(plistlib.dumps(info))
    for name,command in [('sign',['codesign','--force','--sign','-','--pagesize','4096','--identifier',info['CFBundleIdentifier'],str(app)]),
                         ('verify',['codesign','--verify','--strict',str(app)])]:
        r=subprocess.run(command,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
    display=subprocess.run(['codesign','-d','--verbose=4',str(app)],capture_output=True,text=True,check=True).stderr
    match=re.search(r'^CDHash=([a-f0-9]{40})$',display,re.M)
    if not match or not re.search(r'^Page size=4096$',display,re.M):raise ValueError('Signature identity/page size differs')
    raw=exe.read_bytes()
    if struct.unpack_from('<4I',raw)!=(0xfeedfacf,0x100000c,0,2):raise ValueError('Expected ARM64 Mach-O executable')
    at=32;version=None
    for _ in range(struct.unpack_from('<I',raw,16)[0]):
        command,size=struct.unpack_from('<II',raw,at)
        if command==0x32:version=struct.unpack_from('<II',raw,at+8)
        at+=size
    if version!=(2,0x110000):raise ValueError('Expected iOS17 minimum target')
    (out/'result.json').write_text(json.dumps({'compiled':True,'signed_4k':True,'cdhash':match[1],'sha256':sha(exe),
        'source_sha256':sha(source),'command':cmd,'private_entitlements':False,'installed':False,'launched':False,
        'hardware':False,'input_injection':False},indent=2)+'\n')
    print('PASS: signed iOS17 UIKit pen probe; not installed or run')
if __name__=='__main__':main()
