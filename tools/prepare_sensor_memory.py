"""Prepare actual sensor IOKit memory owner and pinned UniXNU4K table inputs."""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion';UP=ROOT/'drivers/EldenSmmu/upstream'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    manifest=json.loads((UP/'source.json').read_text());files={}
    for row in manifest['files'].values():
        original=UP/row['output']
        if sha(original)!=row['output_sha256']:raise ValueError('Pinned ArmSmmu4K source changed')
        if original.suffix in ('.c','.h') or original.name=='LICENSE':shutil.copyfile(original,out/original.name);files[original.name]=sha(original)
    for n in ('NativeMemory.h','NativeMemory.cpp','NativeMemoryMethods.inc','SensorRpc.h','MemoryBinding.h','MemoryBinding.c','PackBinding.h','NativePack.h'):
        shutil.copyfile(DRIVER/n,out/n)
    header=(out/'NativeMemory.h').read_text();header=header.replace('"../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"','"EldenWifiSmmu4KTableMemory.h"');(out/'NativeMemory.h').write_text(header,encoding='utf-8',newline='\n')
    record={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'upstream':manifest,'header_only_relative_include_delta':True,
            'same_qualified_IOMemory_mapping_algorithm':True,'actual_shared_SMMU_route_provider_installed':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared actual retained sensor IOKit memory/PAR/4K tables, no private SMMU owner')
if __name__=='__main__':main()
