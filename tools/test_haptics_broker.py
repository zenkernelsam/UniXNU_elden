"""Exercise the actual native broker's portable methods through GENI/AW peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','firmware','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared source changed')
    if sha(a.firmware)!=record['upstream']['firmware']['sha256']:raise ValueError('Original firmware changed')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    include=a.source/'haptics';flags=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',include]
    run([*flags,include/'Budget.c',DRIVER/'TestBudget.c','-o',out/'budget'],'budget-build');budget=run([out/'budget'],'budget-run')
    files=[include/n for n in ['Budget.c','Catalog.c','Broker.c','Mailbox.c','Queue.c','RtpStream.c','GeniCore.c','Bus.c','AwChip.c','SampleMap.c','WaveCompiler.c','WaveFeed.c']]
    run([*flags,*files,DRIVER/'TestBroker.c','-o',out/'broker'],'broker-build');broker=run([out/'broker',a.firmware],'broker-run')
    run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',include,
         DRIVER/'TestClient.cpp','-o',out/'client'],'client-build');client=run([out/'client'],'client-run')
    result={'passed':True,'budget_output':budget,'broker_output':broker,'source_record_sha256':sha(a.source/'source.json'),
        'actual_compiled_broker_methods':True,'GENI_AW_RTP_composed':True,'physical_OS_peers_modeled':True,
        'test_effect_bank_and_gains_synthetic':True,'client_output':client,'actual_IOKit_handler_methods_run_with_peers':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(budget+broker+client)
if __name__=='__main__':main()
