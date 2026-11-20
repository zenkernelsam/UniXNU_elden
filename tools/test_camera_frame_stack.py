"""Execute original camera Board/DMA/CCI/CAMSS with real retained snapshots."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];CAM=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('frame-source','dma-source','clock-source','power-source','pipeline-source','cci-source','sensor-source','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 for n in ('frame_source','dma_source','clock_source','power_source','pipeline_source','cci_source','sensor_source'):
  folder=getattr(a,n).resolve(strict=True);setattr(a,n,folder);record=json.loads((folder/'source.json').read_text())
  for name,d in record['sources'].items():
   if sha(folder/name)!=d:raise ValueError('Actual input changed')
 sys.path.insert(0,str(CAM));from FrameStackSource import derive
 body,proof=derive();(out/'TestFrameStack.cpp').write_bytes(body)
 def run(cmd,label):
  r=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(label+' failed; original log retained')
  return r.stdout.decode()
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined']
 for folder in (a.frame_source,a.dma_source,a.clock_source,a.power_source,a.pipeline_source,a.cci_source,a.sensor_source):flags+=['-I',folder]
 inputs=[(a.dma_source/'DmaBinding.c','DmaBinding'),(a.clock_source/'Clock.c','Clocks'),(a.clock_source/'Board.c','Board'),(a.pipeline_source/'Pipeline.c','Pipeline'),
  (a.pipeline_source/'CaptureBinding.c','CaptureBinding'),(a.cci_source/'Cci.c','Cci'),(a.cci_source/'SensorBinding.c','SensorBinding'),(a.sensor_source/'Core.c','SensorCore'),
  (CAM/'TestCciPeer.c','CciPeer'),(CAM/'TestPipeline.c','PipelinePeer'),(CAM/'TestClocks.c','ClockPeer'),
  (a.power_source/'Power.c','Power'),(a.power_source/'PowerBus.c','PowerBus'),(a.power_source/'PowerBinding.c','PowerBinding'),(a.power_source/'Platform.c','Platform'),
  (a.power_source/'GeniI2c.c','GeniI2c'),(a.power_source/'Aw99706.c','Aw99706'),(CAM/'TestI2c10Peer.c','BusPeer')]
 for name in json.loads((a.dma_source/'source.json').read_text())['sources']:
  if name.startswith('EldenWifi') and name.endswith('.c'):inputs.append((a.dma_source/name,Path(name).stem))
 objects=[]
 for src,name in inputs:
  options=['-DELDEN_CAMERA_PIPELINE_PEER_API=1','-Wno-unused-function'] if name=='PipelinePeer' else ['-DELDEN_CAMERA_CLOCK_PEER_API=1','-Dmain=previous_clock_main'] if name=='ClockPeer' else []
  obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,*options,'-c',src,'-o',obj],name+'-compile');objects.append(obj)
 # Existing host allocator methods are exactly those in the real snapshot
 # translation unit; rename host-only IOKit types to keep independent models.
 obj=out/'DmaPeer.o';run(['xcrun','clang++','-std=c++17',*flags,'-DELDEN_CAMERA_DMA_PEER_API=1','-Dmain=previous_dma_main','-c',CAM/'TestNativeDma.cpp','-o',obj],'DmaPeer-compile');objects.append(obj)
 model=out/'FramePeer.cpp';text=(CAM/'TestFrameOutput.cpp').read_text();start=text.index('static EldenCameraDmaCapture capture{};');end=text.index('#include "FrameOutputMethods.inc"',start)
 text=text[:start]+text[end:];text=text[:text.index('static void frame(')]
 model.write_text(text)
 obj=out/'FramePeer.o';run(['xcrun','clang++','-std=c++17',*flags,'-DIOBufferMemoryDescriptor=HostOutputDescriptor','-Wno-unused-variable','-c',model,'-o',obj],'FramePeer-compile');objects.append(obj)
 run(['xcrun','clang++','-std=c++17',*flags,out/'TestFrameStack.cpp',*objects,'-o',out/'test'],'host-build')
 outputs={n:run([out/'test',n],n+'-run') for n in ('normal','drain-fail','visibility-fail')}
 (out/'result.json').write_text(json.dumps({'passed':True,'outputs':outputs,'derivation':proof,'actual_done_DMA_output_chain':True,'IOKit_power_GIC_PAR_MMIO_completion_peers_modeled':True,'hardware':False},indent=2)+'\n');print(''.join(outputs.values()))
if __name__=='__main__':main()
