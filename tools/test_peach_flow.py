"""Run prepared QMI function bodies against bounded host transport fixtures."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def function(source,name):
    masked=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),source,flags=re.S)
    start=source.index('int '+name+'(');body=masked.index('{',start);at=body+1;depth=1
    while depth:
        depth+=(masked[at]=='{')-(masked[at]=='}');at+=1
    return source[start:at]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backport',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--wsl-distro');p.add_argument('--compiler',default='cc');a=p.parse_args()
    if a.output.exists():raise ValueError('New output directory required')
    path='drivers/net/wireless/ath/ath12k/qmi.c';data=(a.backport/'tree'/path).read_bytes();report=json.loads((a.backport/'result.json').read_text())
    if hashlib.sha256(data).hexdigest()!=report['files'][path]['output_sha256']:raise ValueError('Prepared QMI source changed')
    source=data.decode();gate='\tif (ab->hw_params->fw.download_tme_lite) {\n\t\tret = ath12k_qmi_apply_ol_cpr(ab, &resp);\n\t\tif (ret)\n\t\t\tgoto out;\n\t}'
    if source.count(gate)!=1:raise ValueError('Target-capability error gate changed')
    template=(ROOT/'drivers/EldenWifi/peach/TestQmiFlow.c').read_text();fragments={}
    for marker,name in [('APPLY_OLCPR','ath12k_qmi_apply_ol_cpr'),('TME_SEND','ath12k_qmi_wlanfw_tme_lite_info_send'),('LOAD_BDF','ath12k_qmi_event_load_bdf')]:
        body=function(source,name);fragments[name]=hashlib.sha256(body.encode()).hexdigest();template=template.replace('// @'+marker+'@',body)
    a.output.mkdir(parents=True);test=a.output/'test.c';binary=a.output/'test';test.write_text(template,newline='\n')
    def convert(p):
        s=str(Path(p).resolve());return '/mnt/'+s[0].lower()+s[2:].replace('\\','/') if a.wsl_distro else s
    prefix=['wsl','-d',a.wsl_distro,'--'] if a.wsl_distro else []
    # The unchanged Linux loop uses int i against bounded u32 num_params.
    flags=[a.compiler,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if a.wsl_distro:flags[1:1]=['-fno-pie','-no-pie']
    compiled=subprocess.run(prefix+flags+[convert(test),'-o',convert(binary)],capture_output=True)
    (a.output/'compiler.stderr').write_bytes(compiled.stderr)
    if compiled.returncode:raise RuntimeError(compiled.stderr.decode('utf-8',errors='backslashreplace'))
    run=subprocess.run(prefix+[convert(binary)],capture_output=True,timeout=20)
    if run.returncode:raise RuntimeError(run.stderr.decode('utf-8',errors='backslashreplace'))
    result={'passed':True,'qmi_sha256':hashlib.sha256(data).hexdigest(),'function_sha256':fragments,'target_capability_gate_checked':True,'stdout':run.stdout.decode(),
            'scope':'Extracted QMI sequencing/copy/error handling; transport and kernel contexts mocked, not TLV encoding or device execution.',
            'hardware_operations':False}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
