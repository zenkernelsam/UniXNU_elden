"""Prepare exact source-bound Gen4 CCI controller code, no hardware operations."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from CciSource import sources
    files,derived=sources()
    for n,b in files.items():(out/n).write_bytes(b)
    for n in ['Cci.c','Cci.h','NativeCci.cpp','NativeCci.h','NativeCciMethods.inc','SensorBinding.c','SensorBinding.h','Core.h']:shutil.copyfile(DRIVER/n,out/n)
    (out/'source.json').write_text(json.dumps({'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},
        'upstream':json.loads((DRIVER/'cci-source.json').read_text()),'derived':derived,
        'clocks_power_pins_CCI_IRQ_ownership_external':True,'hardware':False},indent=2)+'\n')
    print('Prepared exact Gen4 v2 CCI timing/commands/read-write bodies and bounded ownership adapter')
if __name__=='__main__':main()
