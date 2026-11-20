"""Prepare frozen Gen4 camera clock graph and real owned register methods."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from ClockSource import derive
    files,graph,functions=derive()
    for name,b in files.items():(out/name).write_bytes(b)
    for name in ('Clock.c','Clock.h','NativeClocks.h','NativeClocks.cpp','NativeClockMethods.inc','Board.h','Board.c',
                 'PowerBinding.h','Power.h','SharedI2c10.h','NativePipeline.h','SensorBinding.h','NativeCci.h','Cci.h','Pipeline.h','Core.h'):
        shutil.copyfile(DRIVER/name,out/name)
    report={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'graph':graph,'original_functions':functions,
        'fixed_source':json.loads((DRIVER/'clock-source.json').read_text()),'actual_MMIO_power_PLL_owner_installed':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared exact39clock/21RCG/37branch/4domain Gen4 graph and23originalhelpers; real owners mandatory')
if __name__=='__main__':main()
