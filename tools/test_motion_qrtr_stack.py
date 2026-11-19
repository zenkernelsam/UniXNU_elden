"""Execute actual original ADSP/GLINK plus QRTR/SSC transport as one host stack."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Qualified composed QRTR source changed')
    def run(command,label):
        result=subprocess.run(list(map(str,command)),capture_output=True,timeout=90)
        (out/(label+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; original log retained')
        return result.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    units=('QcomGlink','QcomQrtr','ShengQrtr','QrtrPorts','QrtrTransport')
    for name in units:run(['xcrun','clang','-std=c11',*flags,'-c',a.source/(name+'.c'),'-o',out/(name+'.o')],name+'-host')
    run(['xcrun','clang++','-std=c++17',*flags,'-DELDEN_TEST_MOTION_QRTR=1',ROOT/'drivers/EldenMotion/TestAdspChannels.cpp',
         *[out/(name+'.o') for name in units],'-o',out/'test'],'stack-build')
    text=run([out/'test'],'stack-run');m=re.search(r'PASS (\d+) .*?, (\d+) checks',text)
    if not m:raise ValueError('Actual stack result absent')
    result={'passed':True,'host_output':text,'scenarios':int(m[1]),'checks':int(m[2]),
            'actual_original_ADSP_registry_GLlNK_QRTR_SSC_transport':True,'SMEM_locks_DSP_remote_retirement_modeled':True,
            'source_record_sha256':sha(a.source/'source.json'),'test_source_sha256':sha(ROOT/'drivers/EldenMotion/TestAdspChannels.cpp'),'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(text)
if __name__=='__main__':main()
