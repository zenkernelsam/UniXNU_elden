"""Run actual NativeActuator.inc plus broker/live/ring/AW with explicit OS peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','firmware','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();source=a.source.resolve(strict=True);h=source/'haptics';out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(source/name)!=digest:raise ValueError('Prepared source changed')
    if sha(a.firmware)!=record['upstream']['firmware']['sha256']:raise ValueError('Original firmware changed')
    if (h/'NativeActuator.inc').read_bytes()!=(DRIVER/'actuator/NativeActuator.inc').read_bytes():raise ValueError('Actual lifecycle source differs')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',h,'-I',DRIVER/'actuator']
    objects=[];tests={}
    names=['ActuatorLive.c','ActuatorRing.c','SampleMap.c','Budget.c','Catalog.c','Broker.c','Mailbox.c','Queue.c','RtpStream.c','GeniCore.c','Bus.c','AwChip.c']
    for name in names+['TestLifecycleBus.c']:
        path=h/name if name in names else DRIVER/'actuator'/name;obj=out/(Path(name).stem+'.o')
        run(['xcrun','clang','-std=c11',*flags,'-c',path,'-o',obj],Path(name).stem+'-compile');objects.append(obj);tests[str(path)]=sha(path)
    path=DRIVER/'actuator/TestNativeLifecycle.cpp';tests[str(path)]=sha(path)
    run(['xcrun','clang++','-std=c++17',*flags,*objects,path,'-o',out/'test'],'lifecycle-build')
    message=run([out/'test',a.firmware],'lifecycle-run')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'actual_native_lifecycle_included':True,
        'actual_broker_live_ring_AW_GENI':True,'source_record_sha256':sha(source/'source.json'),'test_input_sha256':tests,
        'OS_scheduler_allocation_clock_registers_modeled':True,'positive_profile_HOST_ONLY':True,'hardware':False},indent=2)+'\n');print(message)
if __name__=='__main__':main()
