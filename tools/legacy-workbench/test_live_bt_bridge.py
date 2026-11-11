"""ASan/UBSan coverage of actual live-transport command/reply lifecycle."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];PORT=Path('D:/Desktop/Git/UniXNU_elden')
def wp(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/wcn-native-20261108';out.mkdir(exist_ok=True)
    ref=ROOT/'research/wcn-native-20261108/pagani';bridge=ROOT/'research/wcn-native-20261108/bridge-prepared'
    files=[bridge/'BtBridge.c',bridge/'BtStandIn.c',ref/'BtH4.c',ref/'BtIbs.c',ref/'BtTlv.c',PORT/'drivers/EldenBluetooth/TestLiveBridge.c']
    cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-std=c11','-Wall','-Wextra','-Werror',
         '-Wno-misleading-indentation','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
         '-I',wp(bridge),'-I',wp(ref),*[wp(f) for f in files],'-o',wp(out/'test-bridge')]
    subprocess.run(cmd,check=True,timeout=55)
    r=subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1',
        'UBSAN_OPTIONS=halt_on_error=1',wp(out/'test-bridge')],capture_output=True,text=True,timeout=55)
    print(r.stdout,end='');print(r.stderr,end='',file=sys.stderr);r.check_returncode()
    report={'accepted':True,'hardware_tested':False,'asan':True,'ubsan':True,'result':r.stdout,
            'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
    (out/'host-tests.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
