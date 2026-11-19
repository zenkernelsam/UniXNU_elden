"""Prepare source-bound OS17 Actuator properties and the Elden HAL/ring code."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics/actuator'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from Source import properties
    (out/'Properties.inc').write_bytes(properties())
    for name in ['Abi.h','Ring.c','PlugIn.c','Info.plist','Live.h','Live.c','LICENSE']:shutil.copyfile(DRIVER/name,out/name)
    for name in ['SampleMap.h','SampleMap.c']:shutil.copyfile(DRIVER.parent/name,out/name)
    rows={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
    (out/'source.json').write_text(json.dumps({'sources':rows,'donor':json.loads((DRIVER/'source.json').read_text()),
        'hardware':False,'native_actuator_connected':False},indent=2)+'\n')
    print('Prepared original Actuator properties and Elden48k tagged ring/HAL; native service not yet connected')
if __name__=='__main__':main()
