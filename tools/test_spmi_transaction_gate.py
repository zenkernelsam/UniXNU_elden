"""Run actual ADC/BT reader concurrency/error fixtures on WSL; no device access."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def linux(p):
    p=p.resolve()
    if len(p.drive)!=2 or p.drive[1]!=':':raise ValueError('Windows drive path required')
    return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must stay enabled')
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--dependency',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--distro',default='Ubuntu-22.04');a=ap.parse_args()
    out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    out.mkdir(parents=True);ref=out/'reference';ref.mkdir();plan=json.loads((ROOT/'profiles/v110-bt-fifo/plan.json').read_text())['dependency']
    for n in ('BtPmic.c','BtPmic.h','PaganiBtBoard.h'):
        item=plan['files'][n];b=subprocess.check_output(['git','-C',str(a.dependency),'show',plan['revision']+':'+item['path']]);assert hashlib.sha256(b).hexdigest()==item['sha256'];(ref/n).write_bytes(b)
    patch=module('gate_patch',ROOT/'drivers/EldenSpmi/SourcePatch.py');readpatch=module('done_patch',ROOT/'drivers/EldenBluetooth/SpmiReadPatch.py')
    adc=ROOT/'profiles/v91/sources/power/Spmi.c'
    (out/'Spmi.c').write_bytes(patch.apply('Spmi.c',adc.read_bytes()));(out/'BtPmic.c').write_bytes(patch.apply('BtPmic.c',readpatch.apply((ref/'BtPmic.c').read_bytes())))
    shared=ROOT/'drivers/EldenSpmi';prefix=['wsl','-d',a.distro,'-u','root','--'];results={}
    def run(label,args):
        p=subprocess.run(args,capture_output=True,timeout=90);(out/(label+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(label+' failed; see retained log')
        return p.stdout.decode('utf8',errors='replace')
    def build(label,files):
        target=out/label;cmd=prefix+['cc','-std=c11','-D_XOPEN_SOURCE=700','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-ffunction-sections','-Wl,--gc-sections']
        for p in (shared,ROOT/'drivers/EldenUsbThermal',ROOT/'soc/sm8750/include',ref):cmd+=['-I',linux(p)]
        cmd += [linux(p) for p in files]+['-o',linux(target)];run(label+'-build',cmd);return target,cmd
    for label,files,modes in (
        ('gate',[shared/'TransactionGate.c',shared/'TestTransactionGate.c'],[None]),
        ('old',[shared/'TransactionGate.c',shared/'TestSharedTransactions.c',adc,ref/'BtPmic.c'],['old']),
        ('fixed',[shared/'TransactionGate.c',shared/'TestSharedTransactions.c',out/'Spmi.c',out/'BtPmic.c'],['fixed','error','timeout'])):
        binary,cmd=build(label,files);results[label]={'command':cmd,'sources':{str(p):sha(p) for p in files},'runs':{}}
        for mode in modes:
            name=label+'-'+str(mode);args=prefix+['env','ASAN_OPTIONS=detect_leaks=1','UBSAN_OPTIONS=halt_on_error=1',linux(binary)]+([mode] if mode else [])
            text=run(name,args);results[label]['runs'][str(mode)]=text;print(name+': '+text.strip())
    (out/'result.json').write_text(json.dumps({'passed':True,'hardware_tested':False,'results':results},indent=2)+'\n')
if __name__=='__main__':main()
