"""Prepare source-bound native sensor registry pack cache over accepted RPC core."""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('rpc-source','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.rpc_source=a.rpc_source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    prior=json.loads((a.rpc_source/'source.json').read_text())
    for n,d in prior['sources'].items():
        if sha(a.rpc_source/n)!=d:raise ValueError('Qualified RPC source changed: '+n)
    for n in ('RpcPack.h','RpcPack.c','RpcPrivate.h','RpcCore.c','RpcText.h','SensorRpc.h','SensorRpc.c','LICENSE.RPC'):shutil.copyfile(a.rpc_source/n,out/n)
    for n in ('NativePack.h','NativePack.cpp','NativePackMethods.inc','PackBinding.h','PackBinding.c'):shutil.copyfile(DRIVER/n,out/n)
    r={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'rpc_parent_source_record_sha256':sha(a.rpc_source/'source.json'),
       'original_RPC_pack_source':True,'actual_SSC_GLINK_DMA_provider_installed':False,'actual_Y700_pack_or_identity':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared actual native vnode/SHA256/immutable pack cache, default no input/no startup')
if __name__=='__main__':main()
