"""Execute actual sharedSMMU batch/program/table/touch tests and scalar core."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenSmmu'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Prepared actualSMMUsource changed')
    def run(args,label):
        r=subprocess.run(list(map(str,args)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; exact log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source]
    sources=[a.source/n for n in record['sources'] if n.endswith('.c') and n!='BootRoute.c']
    run(['xcrun','clang',*flags,DRIVER/'TestShared.c',*sources,'-o',out/'test'],'host-build');host=run([out/'test'],'host-run')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':host,'actual_programmer_table_batch_touch':True,
        'source_record_sha256':sha(a.source/'source.json'),'physical_earlyowner_MMIO_IRQ_peers_modeled':True,'hardware':False},indent=2)+'\n');print(host)
if __name__=='__main__':main()
