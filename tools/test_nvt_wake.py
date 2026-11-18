"""Test actual wake decoder and SPI commands with OEM oracle and compile objects."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('oem-audit','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('oem_audit','base_native','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);oracle=json.loads((a.oem_audit/'result.json').read_text())
    if not oracle['passed'] or oracle['actual_commands']!=[[0x50,0x11],[0x50,0x13]]:raise ValueError('OEM oracle incomplete')
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    flags=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
    wake=ROOT/'drivers/EldenWake';protocol=ROOT/'drivers/EldenNvt36536'
    run([*flags,'-DELDEN_WAKE_OEM_FIXTURES=1','-I',a.oem_audit,wake/'Gesture.c',wake/'TestGesture.c','-o',out/'gesture'],'gesture-build')
    g=run([out/'gesture'],'gesture-run')
    run([*flags,protocol/'Protocol.c',wake/'TestCommand.c','-o',out/'command'],'command-build');c=run([out/'command'],'command-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/EldenNvt36536/Protocol.c'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Captured source/header changed')
    kept=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD'):i+=1;continue
        kept.append(remap(v));i+=1
    pobj=out/'EldenNvtProtocol.o';run([*kept,'-mgeneral-regs-only','-Werror',protocol/'Protocol.c','-o',pobj],'protocol-native',remap(job['directory']))
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'Gesture.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-c',wake/'Gesture.c','-o',obj],'gesture-native')
    for path in (pobj,obj):
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',path],path.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected vector code')
        if any(k.endswith('/__mod_init_func') for k in sections(path)):raise ValueError('Automatic wake initializer refused')
    results=json.loads((a.base_native/'build/results.json').read_text());index=cap['jobs'].index(job);oldobj=Path(results[str(index)]['output'])
    before=global_symbols(oldobj);after=global_symbols(pobj)
    if set(after)-set(before)!={'_elden_nvt_low_power_command'} or set(before)-set(after):raise ValueError('Unexpected protocol public delta')
    (out/'result.json').write_text(json.dumps({'passed':True,'gesture_output':g,'command_output':c,'oem_oracle_sha256':sha(a.oem_audit/'result.json'),
        'objects':{p.name:sha(p) for p in (pobj,obj)},'sources':{str(p.relative_to(ROOT)):sha(p) for p in [wake/'Gesture.c',wake/'Gesture.h',protocol/'Protocol.c',protocol/'Protocol.h']},
        'public_protocol_delta':['_elden_nvt_low_power_command'],'GPIO_PDC_provider_connected':False,'full_link':False,'hardware':False},indent=2)+'\n')
    print(g+c)
if __name__=='__main__':main()
