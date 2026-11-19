"""Prepare fixed scalar FastRPC/reverse file service with real-provider gates."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from RpcSource import derive
    generated,deltas=derive()
    for name,text in generated.items():(out/name).write_text(text,encoding='utf-8',newline='\n')
    for name in ('SensorRpc.h','SensorRpc.c'):shutil.copyfile(DRIVER/name,out/name)
    (out/'LICENSE.RPC').write_bytes((DRIVER/'LICENSE.HID').read_bytes())
    record={'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()},
            'upstream':json.loads((DRIVER/'rpc-source.json').read_text()),'qualified_deltas':deltas,'shared_ADSP_or_DMA_provider_installed':False,
            'fixed_donor_RAM_bank_used':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared real source-derived FastRPC/listener/pack with required session/IOVA/stop/calibration inputs')
if __name__=='__main__':main()
