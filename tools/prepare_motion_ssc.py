"""Prepare fixed SSC wire, strict attributes and owned sensor client manager."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from SscSource import derive
    generated,deltas=derive()
    for name,body in generated.items():(out/name).write_text(body,encoding='utf-8',newline='\n')
    for name in ('Ssc.h','Ssc.c','SscCodec.c','Core.h','NativeHid.h','NativeSsc.h','NativeSsc.cpp','NativeSscMethods.inc'):
        shutil.copyfile(DRIVER/name,out/name)
    (out/'LICENSE.SSC').write_bytes((DRIVER/'LICENSE.HID').read_bytes())
    record={'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()},
            'qualified_wire':json.loads((DRIVER/'ssc-source.json').read_text()),'strict_deltas':deltas,
            'actual_shared_QRTR_provider_installed':False,'donor_RPC_heap_or_SMMU_copied':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Prepared qualified SSC schema/strict attributes/owned client; shared transport and real board providers required')
if __name__=='__main__':main()
