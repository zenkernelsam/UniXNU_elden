"""Prepare single original QRTR hooks and actual SSC transport adapter."""
from pathlib import Path
import argparse, hashlib, json, shutil, sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--channel-source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    sys.path.insert(0,str(DRIVER));from QrtrSource import derive,PINS
    original=json.loads((a.channel_source/'source.json').read_text())
    for name,digest in original['sources'].items():
        if sha(a.channel_source/name)!=digest:raise ValueError('Qualified parent channel changed')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for name in original['sources']:shutil.copyfile(a.channel_source/name,out/name)
    source,changes=derive()
    for name,body in source.items():(out/name).write_text(body,encoding='utf-8',newline='\n')
    body=(out/'AdspProbe.cpp').read_text();anchor='void sheng_qrtr_start(void);'
    if body.count(anchor)!=1:raise ValueError('Original worker declaration changed')
    body=body.replace(anchor,anchor+'\nvoid elden_qrtr_original_service(void);')
    anchor='    pumpSends();\n    uint32_t up = 0;'
    if body.count(anchor)!=1:raise ValueError('Original DSP executor pump boundary changed')
    body=body.replace(anchor,'    pumpSends();\n    elden_qrtr_original_service();\n    uint32_t up = 0;')
    (out/'AdspProbe.cpp').write_text(body,encoding='utf-8',newline='\n')
    for name in ('QrtrPorts.h','QrtrPorts.c','QrtrTransport.h','QrtrTransport.c','NativeSsc.h','Ssc.h','Core.h'):
        shutil.copyfile(DRIVER/name,out/name)
    record={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'fixed_elden_QRTR_inputs':PINS,
            'channel_parent_record_sha256':sha(a.channel_source/'source.json'),
            'qualified_QRTR_donor':json.loads((DRIVER/'qrtr-source.json').read_text()),'exact_deltas':changes,
            'existing_sole_IPCRTR_owner':True,'layout_consumers_rebuild':['AdspProbe.o','ShengQrtr.o','QcomQrtr.o'],
            'real_owner_receive_intents_required':True,'real_remote_retirement_provider':False,'automatic_SSC_start':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Prepared original QRTR client ports/owed flow control and actual SSC transport; real owner inputs required')
if __name__=='__main__':main()
