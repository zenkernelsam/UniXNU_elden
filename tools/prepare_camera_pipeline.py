"""Prepare hash-bound exact SM8750 CAMSS register/queue functions."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from PipelineSource import derive
    sources,record=derive()
    for name,body in sources.items():(out/name).write_bytes(body)
    for name in ('Pipeline.h','Pipeline.c','Core.h','NativePipeline.h','NativePipeline.cpp','NativePipelineMethods.inc','CaptureBinding.c','SensorBinding.h','NativeCci.h','Cci.h','TestIrqPeer.inc'):
        shutil.copyfile(DRIVER/name,out/name)
    result={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'fixed_source':json.loads((DRIVER/'pipeline-source.json').read_text()),
        'actual_function_derivations':record,'hardware':False,'real_power_DMA_IRQ_owner_installed':False,'synthetic_frame_producer':False}
    (out/'source.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared exact TB322FC PHY2.4/CSID980/VFEgen4 register and queue bodies; no hardware defaults')
if __name__=='__main__':main()
