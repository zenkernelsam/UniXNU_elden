"""Generate exact fixed Gen4 AW86937 core from vendored, hash-bound sources."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from AwSource import derive
    (out/'AwChip.c').write_bytes(derive())
    for name in ['AwChip.h','AwKernelShim.h']:shutil.copyfile(DRIVER/name,out/name)
    rows={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
    (out/'source.json').write_text(json.dumps({'sources':rows,'upstream':json.loads((DRIVER/'source.json').read_text()),
        'single_shot_delta':'SEQ1LOOP infinite15 to0 only','native_bus_connected':False,'hardware':False},indent=2)+'\n')
    print('Generated fixed Gen4 AW bodies with explicit owning-bus adapter; no hardware')
if __name__=='__main__':main()
