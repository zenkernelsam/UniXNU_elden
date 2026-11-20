"""Run actual retained memory/cache/RPC and SSC worker with new binding/observer."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--motion-core',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    a.motion_core=a.motion_core.resolve(strict=True);core=json.loads((a.motion_core/'source.json').read_text())
    for name,digest in core['sources'].items():
        if sha(a.motion_core/name)!=digest:raise ValueError('Qualified original motion core changed')
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Qualified runtime source changed')
    def run(command,label):
        result=subprocess.run(list(map(str,command)),capture_output=True,timeout=90);(out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; original log retained')
        return result.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    memory=[];ssc=[]
    for name in ('EldenWifiSmmu4K','EldenWifiSmmu4KContext','EldenWifiSmmu4KAudit','EldenWifiSmmu4KProgram','EldenWifiSmmu4KTableMemory',
                 'MemoryBinding','RpcCore','RpcPack','SensorRpc','PackBinding'):
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',a.source/(name+'.c'),'-o',obj],name+'-compile');memory.append(obj)
    run(['xcrun','clang++','-std=c++17','-Wno-deprecated-declarations',*flags,'-I',DRIVER,'-DELDEN_TEST_MEMORY=1',
         DRIVER/'TestNativePack.cpp',*memory,'-o',out/'memory-test'],'memory-build')
    memory_output=run([out/'memory-test'],'memory-run')
    for name in ('Ssc','SscCodec','SscWire','SscPb'):
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',a.source/(name+'.c'),'-o',obj],name+'-compile');ssc.append(obj)
    obj=out/'MotionCore.o';run(['xcrun','clang','-std=c11',*flags,'-c',a.motion_core/'Core.c','-o',obj],'MotionCore-compile');ssc.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestNativeSsc.cpp',*ssc,'-o',out/'ssc-test'],'ssc-build')
    ssc_output=run([out/'ssc-test'],'ssc-run')
    router=[]
    for name in ('QcomQrtr','ShengQrtr','QrtrPorts','QrtrTransport'):
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',a.source/(name+'.c'),'-o',obj],name+'-compile');router.append(obj)
    run(['xcrun','clang++','-std=c++17','-Wno-deprecated-declarations',*flags,'-I',DRIVER,'-DELDEN_TEST_MEMORY=1','-DELDEN_TEST_SENSOR_RUNTIME=1',
         DRIVER/'TestNativePack.cpp',*memory,*ssc,*router,'-o',out/'runtime-stack-test'],'runtime-stack-build')
    runtime_output={name:run([out/'runtime-stack-test','runtime',name],'runtime-stack-'+name) for name in ('normal','retire-fail')}
    result={'passed':True,'actual_memory_cache_RPC_output':memory_output,'actual_SSC_worker_observer_output':ssc_output,
        'actual_composed_runtime_stack_output':runtime_output,
        'physical_IOKit_VFS_GLINK_router_clock_HID_peers_modeled':True,'source_record_sha256':sha(a.source/'source.json'),'motion_core_record_sha256':sha(a.motion_core/'source.json'),'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(memory_output+ssc_output+''.join(runtime_output.values()))
if __name__=='__main__':main()
