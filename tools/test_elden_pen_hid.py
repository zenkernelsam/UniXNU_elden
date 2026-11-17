"""Execute real pen HID encoding and pressure hook with explicit host peers."""
from pathlib import Path
import argparse,json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenPen'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);results={}
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',DRIVER]
    run(['xcrun','clang','-std=c11',*flags,'-c',DRIVER/'HidReport.c','-o',out/'HidReport.o'],'wire-build')
    run(['xcrun','clang','-std=c11',*flags,'-c',DRIVER/'Tilt.c','-o',out/'Tilt.o'],'tilt-build')
    run(['xcrun','clang','-std=c11',*flags,DRIVER/'TestHid.c',out/'HidReport.o','-o',out/'hid-test'],'hid-build')
    results['wire']=run([out/'hid-test'],'hid-run')
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestPressure.cpp',out/'HidReport.o',out/'Tilt.o','-o',out/'pressure-test'],'pressure-build')
    results['pressure']=run([out/'pressure-test'],'pressure-run')
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [DRIVER/n for n in ('HidReport.c','HidReport.h','PressureHook.inc','PoseHook.inc','Tilt.c','Tilt.h','TiltConstants.h','TestHid.c','TestPressure.cpp')]}
    (out/'result.json').write_text(json.dumps({'passed':True,'results':results,'sources':hashes,'actual_hook':True,'OS_peers_modeled':True,'hardware':False},indent=2)+'\n')
    print(''.join(results.values()))
if __name__=='__main__':main()
