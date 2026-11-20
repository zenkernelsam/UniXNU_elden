"""Execute actual shared native MMIO/GIC + camera/sensor allocator composition."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','camera-source','sensor-source','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    folders={}
    for n in ('source','camera_source','sensor_source'):
        folder=getattr(a,n).resolve(strict=True);setattr(a,n,folder);record=json.loads((folder/'source.json').read_text());folders[n]=sha(folder/'source.json')
        for name,digest in record['sources'].items():
            if sha(folder/name)!=digest:raise ValueError('Exact prepared source changed: '+name)
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source,'-I',a.camera_source,'-I',a.sensor_source]
    core=ROOT/'drivers/EldenSmmu';objects=[]
    sources=[a.source/n for n in json.loads((a.source/'source.json').read_text())['sources'] if n.endswith('.c') and n!='BootRoute.c']
    sources.append(core/'TestSharedNativeRegistry.c')
    for f in sources:
        obj=out/(f.stem+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',f,'-o',obj],f.stem+'-compile');objects.append(obj)
    for name,folder,source,options in [('camera',a.camera_source,ROOT/'drivers/EldenCamera/TestNativeDma.cpp',['-DELDEN_CAMERA_DMA_PEER_API=1','-DIOBufferMemoryDescriptor=TestCameraBuffer','-DIOMemoryMap=TestCameraMap']),
            ('sensor',a.sensor_source,ROOT/'drivers/EldenMotion/TestNativeMemory.cpp',['-DELDEN_SENSOR_MEMORY_PEER_API=1','-DIOBufferMemoryDescriptor=TestSensorBuffer','-DIOMemoryMap=TestSensorMap'])]:
        obj=out/(name+'.o');local=['-I',folder,*flags];run(['xcrun','clang++','-std=c++17',*local,*options,'-Dmain=old_'+name+'_main','-c',source,'-o',obj],name+'-compile');objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,core/'TestSharedNative.cpp',*objects,'-o',out/'test'],'host-build')
    cases=('normal','early','bank','irq','irq-flags','mapping','foreign','visibility','global-fault','context-fault','tlb')
    results={n:run([out/'test',n],n) for n in cases}
    report={'passed':True,'scenarios':len(cases),'results':results,'source_records':folders,'actual_native_MMIO_GIC_allocators_batch_touch':True,
            'IOKit_PAR_power_ACTLR_memory_GIC_MMIO_peers_modeled':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(''.join(results.values()))
if __name__=='__main__':main()
