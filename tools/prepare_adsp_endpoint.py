"""Prepare concrete original ADSP endpoint adapter and actual epoch header."""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('channel-source','memory-source','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('channel_source','memory_source'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    parents={}
    for folder in (a.channel_source,a.memory_source):
        r=json.loads((folder/'source.json').read_text());parents[str(folder)]=sha(folder/'source.json')
        for n,d in r['sources'].items():
            if sha(folder/n)!=d:raise ValueError('Qualified endpoint parent source changed')
            if n.endswith('.h'):
                dest=out/n
                if dest.exists() and dest.read_bytes()!=(folder/n).read_bytes():raise ValueError('Header collision: '+n)
                shutil.copyfile(folder/n,dest)
    for name in ('AdspEndpoint.h','AdspEndpoint.cpp','AdspEndpointMethods.inc'):shutil.copyfile(DRIVER/name,out/name)
    (out/'LICENSE').write_bytes((DRIVER/'LICENSE.HID').read_bytes())
    result={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'parents':parents,
            'actual_existing_ADSP_registry_adapter':True,'source_memory_graph_headers':True,'hardware':False,'actual_retirement_provider':False}
    (out/'source.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared actual ADSP epoch endpoint/RPC adapter; physical owner retirement provider required')
if __name__=='__main__':main()
