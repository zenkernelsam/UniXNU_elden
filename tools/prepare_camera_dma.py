"""Prepare camera allocator/PAR checks and exact upstream4K table sources."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera';UP=ROOT/'drivers/EldenSmmu/upstream'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(DRIVER));from DmaSource import derive
    generated,checks=derive()
    for name,b in generated.items():(out/name).write_bytes(b)
    for name in ('NativeDma.h','NativeDma.cpp','NativeDmaMethods.inc','DmaBinding.h','DmaBinding.c','Pipeline.h','Core.h','Board.h','NativeClocks.h','Clock.h',
                 'PowerBinding.h','Power.h','SharedI2c10.h','NativePipeline.h','SensorBinding.h','NativeCci.h','Cci.h'):
        shutil.copyfile(DRIVER/name,out/name)
    text=(out/'NativeDma.h').read_text(encoding='utf-8').replace('"../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"','"EldenWifiSmmu4KTableMemory.h"')
    (out/'NativeDma.h').write_text(text,encoding='utf-8',newline='\n')
    dma_spec=json.loads((DRIVER/'dma-source.json').read_text());table=json.loads((UP/'source.json').read_text())
    if sha(UP/'source.json')!=dma_spec['table_manifest_sha256'] or table['revision']!=dma_spec['ArmSmmu4K_revision']:
        raise ValueError('Qualified original SMMU table manifest changed')
    for row in table['files'].values():
        source=UP/row['output']
        if sha(source)!=row['output_sha256']:raise ValueError('Pinned original SMMU table source changed')
        if source.suffix in ('.c','.h') or source.name=='LICENSE':shutil.copyfile(source,out/source.name)
    # IDs are generated from frozen source, no checked-in guessed native ABI.
    from ClockSource import derive as clocks
    ids=clocks()[0]['ClockIds.h'];(out/'ClockIds.h').write_bytes(ids)
    record={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'same_original_allocator_helpers':checks,
        'table_upstream':table,'input_source':json.loads((DRIVER/'dma-source.json').read_text()),
        'actual_shared_earlySMMU_provider_installed':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared actual camera guarded memory/highPA→32bitIOVA and original4K/PAR/cache helpers; no device/route startup')
if __name__=='__main__':main()
