"""Execute real snapshot/ticket lifetime and independent full10bit preview tests."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Prepared real frame source changed')
    def run(args,label):
        r=subprocess.run(list(map(str,args)),capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source]
    run(['xcrun','clang','-std=c11',*flags,ROOT/'drivers/EldenCamera/TestRaw10.c',a.source/'Raw10.c','-o',out/'raw10'],'raw-build')
    raw=run([out/'raw10'],'raw-run')
    run(['xcrun','clang++','-std=c++17',*flags,ROOT/'drivers/EldenCamera/TestFrameOutput.cpp','-o',out/'snapshots'],'snapshot-build')
    results={n:run([out/'snapshots',n],n+'-run') for n in ('normal','allocation','release','queue')}
    run(['xcrun','clang++','-std=c++17',*flags,ROOT/'drivers/EldenCamera/TestService.cpp','-o',out/'service'],'service-build')
    service=run([out/'service'],'service-run')
    run(['xcrun','clang','-std=c11',*flags,'-DELDEN_CAMERA_CLIENT_TEST=1',ROOT/'drivers/EldenCamera/TestCameraClient.c','-o',out/'client'],'client-build')
    clients={n:run([out/'client',n],n+'-client-run') for n in ('normal','unmap','map','convert','metadata')}
    (out/'result.json').write_text(json.dumps({'passed':True,'raw10':raw,'snapshots':results,'service':service,'clients':clients,'sources':record['sources'],
        'IOKit_PAR_power_completion_peers_modeled':True,'conversion_pixels_synthetic_host_fixture':True,'hardware':False},indent=2)+'\n');print(raw+''.join(results.values())+service+''.join(clients.values()))
if __name__=='__main__':main()
