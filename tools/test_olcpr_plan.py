"""Check the no-I/O OL-CPR planner against pinned Linux arithmetic and DTS."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('linux-commit-json','board-dts','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--wsl-distro');p.add_argument('--compiler',default='cc');a=p.parse_args()
    if a.output.exists():raise ValueError('New output directory required')
    commit=json.loads(a.linux_commit_json.read_text())
    if commit['sha']!='dc7d14dc64c8d4f86220b30cd126b17d805c8474':raise ValueError('Wrong Linux origin')
    f=next(f for f in commit['files'] if f['filename']=='drivers/power/sequencing/pwrseq-qcom-wcn.c')
    added='\n'.join(line[1:] for line in f['patch'].splitlines() if line.startswith('+'))+'\n'
    start=added.index('static int pwrseq_qcom_wcn_adjust_voltage(');end=added.index('\n}\n',start)+3;reference=added[start:end]
    if hashlib.sha256(reference.encode()).hexdigest()!='f960bbe1d70be4808a3455a49c23653ca1af95a13058179353dbee3b244aea18':raise ValueError('Reference arithmetic changed')
    if sha(a.board_dts)!='0a4361f3f2225befd954dd5420216f357bfa8a68294c28c895ec2478ebea3ba3':raise ValueError('Wrong TB322FC DTS')
    dts=a.board_dts.read_text();maps={}
    for name in ('vreg-pdc-map','pmu-vreg-map'):
        found=re.findall(r'qcom,'+name+r'\s*=\s*(.*?);',dts,re.S)
        if len(found)!=1:raise ValueError('Ambiguous DTS map')
        maps[name]=dict(s.split(':') for s in re.findall(r'"([^"]+)"',found[0]))
    if maps['vreg-pdc-map']!={'s5f':'rf','s4d':'bb','s3g':'rf','s7i':'rf'}:raise ValueError('Board PDC map differs')
    expected=dict(zip(('VDDD_AON_0P9','VDDA_RFA_1P9','VDDA_RFA_1P3','VDDA_RFA_0P9','VDDD_WLMX_0P9','VDDD_WLCX_0P9','VDDD_BTCX_0P9','VDDD_BTCMX_0P9','VDDA_PCIE_1P2','VDDA_PCIE_0P9'),('s5f','s3g','s7i','s5f','s4d','s5f','s5f','s5f','s7i','s7i')))
    if maps['pmu-vreg-map']!=expected:raise ValueError('Board PMU aliases differ')
    a.output.mkdir(parents=True)
    (a.output/'LinuxOlCprReference.h').write_text('#include <stdint.h>\n#include <errno.h>\ntypedef uint32_t u32;\n#define check_add_overflow(a,b,out) __builtin_add_overflow(a,b,out)\n'+reference)
    def path(value):
        value=str(Path(value).resolve())
        if a.wsl_distro:return '/mnt/'+value[0].lower()+value[2:].replace('\\','/')
        return value
    binary=a.output/'test-olcpr';source=ROOT/'drivers/EldenWifi'
    command=[a.compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+path(source),'-I'+path(a.output),path(source/'TestOlCprPlan.c'),path(source/'OlCprPlan.c'),'-o',path(binary)]
    prefix=['wsl','-d',a.wsl_distro,'--'] if a.wsl_distro else []
    if a.wsl_distro:command[1:1]=['-fno-pie','-no-pie']
    build=subprocess.run(prefix+command,capture_output=True)
    (a.output/'compiler.stdout').write_bytes(build.stdout);(a.output/'compiler.stderr').write_bytes(build.stderr)
    if build.returncode:raise RuntimeError(build.stderr.decode('utf-8',errors='backslashreplace'))
    result=subprocess.run(prefix+[path(binary)],capture_output=True,timeout=20)
    if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='backslashreplace'))
    report={'passed':True,'source_revision':commit['sha'],'dts_sha256':sha(a.board_dts),'maps':maps,
            'linux_arithmetic_sha256':hashlib.sha256(reference.encode()).hexdigest(),
            'source_sha256':{n:sha(source/n) for n in ('OlCprPlan.c','OlCprPlan.h','TestOlCprPlan.c')},
            'stdout':result.stdout.decode(),'stderr':result.stderr.decode(),'hardware_operations':False,'applied_voltage':False}
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
