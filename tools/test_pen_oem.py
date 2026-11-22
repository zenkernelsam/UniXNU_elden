"""Compare actual production decoding with original instruction fixtures and
reproduce/fix the real native packet callback with host sender/clock peers.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenPen'
OLD='ab49889'
def function(text,signature):
    start=text.index(signature);at=text.index('{',start)+1;depth=1
    while depth:depth+=(text[at]=='{')-(text[at]=='}');at+=1
    return text[start:at]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);oracle=json.loads((a.fixtures/'result.json').read_text())
    if not oracle['passed'] or hashlib.sha256((a.fixtures/'OemFixtures.h').read_bytes()).hexdigest()!=oracle['fixture_header_sha256']:raise ValueError('Oracle header differs')
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined'];results={}
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; log retained')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11',*flags,'-I',a.fixtures,DRIVER/'PenReport.c',DRIVER/'TestOem.c','-o',out/'oem-test'],'oracle-build')
    results['oracle']=run([out/'oem-test'],'oracle-run')
    for label,rev in [('original',OLD),('fixed',None)]:
        folder=out/label;folder.mkdir();objects=[]
        headers=('PenReport.c','PenReport.h','Delivery.c','Delivery.h') if rev else (
            'PenReport.c','PenReport.h','Delivery.c','Delivery.h',
            'Owner.c','Owner.h','NativeBridge.c','NativeBridge.h','NativeRoute.c','NativeRoute.h')
        for name in headers:
            data=subprocess.check_output(['git','-C',str(ROOT),'show',rev+':drivers/EldenPen/'+name]) if rev else (DRIVER/name).read_bytes()
            (folder/name).write_bytes(data)
        units=('PenReport.c','Delivery.c') if rev else ('PenReport.c','Delivery.c','Owner.c','NativeBridge.c','NativeRoute.c')
        for name in units:
            obj=folder/(Path(name).stem+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',folder/name,'-o',obj],label+'-'+name);objects.append(obj)
        native=subprocess.check_output(['git','-C',str(ROOT),'show',OLD+':drivers/EldenPen/EldenPenHid.cpp']).decode() if rev else (DRIVER/'EldenPenHid.cpp').read_text()
        body=function(native,'extern "C" int elden_pen_native_packet(');(folder/'ActualPacket.inc').write_text(body+'\n')
        # Stage the test beside the selected headers; quoted includes must not
        # accidentally consume the current header for the original28-byte frame.
        harness=folder/'Test.cpp';harness.write_bytes((DRIVER/'TestMetadataRegression.cpp').read_bytes())
        run(['xcrun','clang++','-std=c++17',*flags,'-DORIGINAL='+str(int(bool(rev))),harness,*objects,'-o',folder/'test'],label+'-build')
        results[label]=run([folder/'test'],label+'-run')
    report={'passed':True,'oracle':oracle,'results':results,'actual_native_callback':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(''.join(results.values()))
if __name__=='__main__':main()
