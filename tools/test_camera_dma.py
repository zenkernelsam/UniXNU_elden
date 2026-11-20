"""Execute actual camera DMA allocation/state/guard graph against physical peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Qualified prepared camera DMA input changed')
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source];objects=[]
    for n in record['sources']:
        if n.endswith('.c') and n!='DmaBinding.c':
            obj=out/(Path(n).stem+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',a.source/n,'-o',obj],Path(n).stem+'-compile');objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestNativeDma.cpp',*objects,'-o',out/'test'],'host-build')
    output=run([out/'test'],'host-run');result={'passed':True,'host_output':output,'prepared_source_sha256':sha(a.source/'source.json'),
        'actual_original4K_guarded_camera_native_methods':True,'physical_IOKit_cache_PAR_early_SMMU_peers_modeled':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(output)
if __name__=='__main__':main()
