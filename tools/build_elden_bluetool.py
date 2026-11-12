"""Build the owned 21H461-compatible BlueTool service; never install or run it."""
import argparse,hashlib,json,plistlib,re,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def verify_target(path):
    b=path.read_bytes()
    if struct.unpack_from('<4I',b)!=(0xfeedfacf,0x100000c,0,2):raise ValueError('Expected thin arm64 executable, not arm64e')
    at=32;minimum=None
    for _ in range(struct.unpack_from('<I',b,16)[0]):
        cmd,size=struct.unpack_from('<II',b,at)
        if cmd==0x32:
            platform,minimum=struct.unpack_from('<II',b,at+8)
            if platform!=2:raise ValueError('Expected iOS platform')
        elif cmd==0x25:minimum=struct.unpack_from('<I',b,at+8)[0]
        at+=size
    if minimum!=0x110000:raise ValueError('Expected iOS17.0 minimum')
    return {'architecture':'arm64','minimum_os':'17.0','sha256':sha(path)}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();source=ROOT/'drivers/EldenBluetooth/userland'
    if out.exists():raise ValueError('Output must be new')
    out.mkdir(parents=True)
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
    command=['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios17.0','-isysroot',sdk,
             '-O2','-Wall','-Wextra','-Werror','-fblocks','-fstack-protector-strong','-I',str(source.parent),
             str(source/'BlueTool.c'),str(source/'BootReply.c'),'-o',str(out/'BlueTool')]
    subprocess.run(command,check=True)
    entitlements=out/'BlueTool.entitlements.plist'
    entitlements.write_bytes(plistlib.dumps({'com.apple.private.security.no-container':True}))
    subprocess.run(['codesign','--force','--sign','-','--pagesize','4096','--identifier','org.unixnu.elden.BlueTool',
                    '--entitlements',str(entitlements),str(out/'BlueTool')],check=True)
    subprocess.run(['codesign','--verify','--strict',str(out/'BlueTool')],check=True)
    target=verify_target(out/'BlueTool')
    detail=subprocess.run(['codesign','--display','--verbose=4',str(out/'BlueTool')],capture_output=True,text=True,check=True).stderr
    cdhash=re.search(r'^CDHash=([0-9a-f]{40})$',detail,re.M)
    if not cdhash:raise ValueError('Missing signature CDHash')
    job={'Label':'com.apple.BlueTool','ProgramArguments':['/usr/sbin/BlueTool','-R'],
         'MachServices':{'com.apple.BlueTool':True},'UserName':'root','POSIXSpawnType':'Interactive',
         'EnableTransactions':True,'EnablePressuredExit':False,'ThrottleInterval':3}
    (out/'com.apple.BlueTool.plist').write_bytes(plistlib.dumps(job))
    result={'target':'arm64-apple-ios17.0','target_verified':target,'cdhash':cdhash[1],'files':{n:sha(out/n) for n in ('BlueTool','com.apple.BlueTool.plist','BlueTool.entitlements.plist')},
            'sources':{str(p.relative_to(ROOT)):sha(p) for p in (source/'BlueTool.c',source/'BootReply.c',source/'BootReply.h',source.parent/'BootstrapStatus.h')},
            'command':command,'hardware_tested':False,'installed':False,'requires_kernel_status':'kern.elden_bt_bootstrap',
            'active_deepsleep_supported':False,'bluetoothd_modified':False}
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
