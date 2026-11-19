"""Prepare the single original Elden ADSP registry and source-bound GLINK close."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion';POWER=ROOT/'drivers/EldenPmicGlink'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER));from AdspChannelSource import derive,PINS
    source,deltas=derive()
    for n,body in source.items():(out/n).write_text(body,encoding='utf-8',newline='\n')
    for n in ('AdspChannel.h','AdspRegistryMethods.inc','AdspPublicMethods.inc'):shutil.copyfile(DRIVER/n,out/n)
    for n in ('QcomMdt.h','QcomQmp.h','QcomRpmh.h','QcomScm.h','QcomSmem.h','QcomSmp2p.h'):shutil.copyfile(POWER/n,out/n)
    # Execute these actual methods/structs against host IOLock/GLINK peers.
    text=source['AdspProbe.cpp'];begin=text.index('struct ShengAdspChannel {');end=text.index('\nnamespace {\nShengAdspChannel',begin)
    (out/'AdspChannelStruct.inc').write_text(text[begin:end],encoding='utf-8',newline='\n')
    record={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'fixed_elden_inputs':PINS,
            'qualified_GLlNK_donor':json.loads((DRIVER/'glink-source.json').read_text()),'exact_deltas':deltas,
            'single_original_ADSP_owner':True,'GLlNK_type_layout_rebuild_required':True,'hardware':False,'physical_DSP_or_DMA_stop_qualified':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared original ADSP epoch/status/close registry and fixed GLINK, old power/IRQ/firmware paths retained')
if __name__=='__main__':main()
