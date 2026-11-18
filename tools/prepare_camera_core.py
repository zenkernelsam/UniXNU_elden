"""Prepare exact Linux sensor bodies/tables and the owned-camera adapter."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from Source import sources
    code,meta=sources();(out/'Core.c').write_bytes(code)
    for name in ['Core.h','CoreShim.h']:shutil.copyfile(DRIVER/name,out/name)
    (out/'source.json').write_text(json.dumps({'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()},
        'upstream':json.loads((DRIVER/'source.json').read_text()),'extracted_bodies_unchanged':meta,
        'native_CCI_CAMSS_provider_connected':False,'hardware':False},indent=2)+'\n')
    print('Prepared actual Gen4 sensor tables/power/control/stream bodies and mandatory owned transport/receiver callbacks')
if __name__=='__main__':main()
