"""Execute real native clock/board/shared-WL/sensor/CCI/CAMSS composition."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('clock-source','power-source','pipeline-source','cci-source','sensor-source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for name in ('clock_source','power_source','pipeline_source','cci_source','sensor_source'):
        folder=getattr(a,name).resolve(strict=True);setattr(a,name,folder);record=json.loads((folder/'source.json').read_text())
        for filename,digest in record['sources'].items():
            if sha(folder/filename)!=digest:raise ValueError('Actual source differs: '+filename)
    sys.path.insert(0,str(DRIVER));from BoardStackSource import derive
    body,derivation=derive();(out/'TestBoardStack.cpp').write_bytes(body)
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original logs retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined']
    for folder in (a.clock_source,a.power_source,a.pipeline_source,a.cci_source,a.sensor_source):flags.extend(['-I',folder])
    inputs=[(a.clock_source/'Clock.c','Clocks'),(a.clock_source/'Board.c','Board'),(a.pipeline_source/'Pipeline.c','Pipeline'),(a.pipeline_source/'CaptureBinding.c','CaptureBinding'),
        (a.cci_source/'Cci.c','Cci'),(a.cci_source/'SensorBinding.c','SensorBinding'),(a.sensor_source/'Core.c','SensorCore'),
        (DRIVER/'TestCciPeer.c','CciPeer'),(DRIVER/'TestPipeline.c','PipelinePeer'),(DRIVER/'TestClocks.c','ClockPeer'),
        (a.power_source/'Power.c','Power'),(a.power_source/'PowerBus.c','PowerBus'),(a.power_source/'PowerBinding.c','PowerBinding'),
        (a.power_source/'Platform.c','Platform'),(a.power_source/'GeniI2c.c','GeniI2c'),(a.power_source/'Aw99706.c','Aw99706'),(DRIVER/'TestI2c10Peer.c','BusPeer')]
    objects=[]
    for source,name in inputs:
        options=['-DELDEN_CAMERA_PIPELINE_PEER_API=1','-Wno-unused-function'] if name=='PipelinePeer' else ['-DELDEN_CAMERA_CLOCK_PEER_API=1','-Dmain=previous_clock_main'] if name=='ClockPeer' else []
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,*options,'-c',source,'-o',obj],name+'-compile');objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,out/'TestBoardStack.cpp',*objects,'-o',out/'test'],'host-build')
    outputs={name:run([out/'test',name],name+'-run') for name in ('normal','drain-fail')}
    (out/'result.json').write_text(json.dumps({'passed':True,'outputs':outputs,'derivation':derivation,
        'actual_native_clocks_board_WL_sensor_CCI_CAMSS':True,'physical_PLL_RPMh_MMIO_IRQ_DMA_peers_modeled':True,'hardware':False},indent=2)+'\n');print(''.join(outputs.values()))
if __name__=='__main__':main()
