"""Execute original sensor/CCI/native pipeline capture and power-stop ordering."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('pipeline-source','cci-source','sensor-source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for name in ('pipeline_source','cci_source','sensor_source'):
        folder=getattr(a,name).resolve(strict=True);setattr(a,name,folder);record=json.loads((folder/'source.json').read_text())
        for filename,digest in record['sources'].items():
            if sha(folder/filename)!=digest:raise ValueError('Qualified component source changed')
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.pipeline_source,'-I',a.cci_source,'-I',a.sensor_source]
    inputs=[(a.pipeline_source/'Pipeline.c','Pipeline'),(a.pipeline_source/'CaptureBinding.c','CaptureBinding'),(a.cci_source/'Cci.c','Cci'),
            (a.cci_source/'SensorBinding.c','SensorBinding'),(a.sensor_source/'Core.c','SensorCore'),(DRIVER/'TestCciPeer.c','CciPeer'),(DRIVER/'TestPipeline.c','PipelinePeer')]
    objects=[]
    for source,name in inputs:
        options=['-DELDEN_CAMERA_PIPELINE_PEER_API=1','-Wno-unused-function'] if name=='PipelinePeer' else []
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,*options,'-c',source,'-o',obj],name+'-compile');objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestCaptureStack.cpp',*objects,'-o',out/'test'],'host-build')
    outputs={name:run([out/'test',name],name+'-run') for name in ('normal','drain-fail')}
    (out/'result.json').write_text(json.dumps({'passed':True,'outputs':outputs,'actual_sensor_CCI_nativepipeline_binding':True,
        'power_MMIO_GIC_DMA_peers_modeled':True,'hardware':False},indent=2)+'\n');print(''.join(outputs.values()))
if __name__=='__main__':main()
