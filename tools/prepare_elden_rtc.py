"""Prepare hash-bound RTC/clock sources; no build or device access."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenRtc'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from Source import core,platform
    (out/'Core.c').write_bytes(core());(out/'ArmDTPlatform.cpp').write_bytes(platform())
    for name in ('Core.h','CoreShim.h','Spmi.c','Spmi.h','Native.cpp','Owner.c','Owner.h'):shutil.copyfile(DRIVER/name,out/name)
    for name in ('TransactionGate.c','TransactionGate.h'):shutil.copyfile(ROOT/'drivers/EldenSpmi'/name,out/name)
    rows={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
    (out/'source.json').write_text(json.dumps({'sources':rows,'upstream':json.loads((DRIVER/'source.json').read_text()),'persistent_offset_verified':False,'hardware':False},indent=2)+'\n')
    print('Prepared actual Gen4 RTC read, shared SPMI and preserved-platform clock integration')
if __name__=='__main__':main()
