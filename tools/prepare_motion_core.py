"""Prepare bounded source-derived SSC protocol/math without donor hardware/fakes."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from Source import derive
    code,header,deltas=derive();(out/'Core.c').write_text(code);(out/'SscPrivate.h').write_text(header);shutil.copyfile(DRIVER/'Core.h',out/'Core.h')
    (out/'source.json').write_text(json.dumps({'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()},
        'upstream':json.loads((DRIVER/'source.json').read_text()),'parser_hardening_deltas':deltas,
        'SM8250_MMIO_or_fake_gravity_copied':False,'exact_Gen4_transport_native_connected':False,'hardware':False},indent=2)+'\n')
    print('Prepared actual source-derived QMI/SSC, bounded parser/math/mount/gravity classifier; no transport or synthetic producer')
if __name__=='__main__':main()
