"""Build actual OS17 native camera clock MMIO and board composition objects."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','base-native','parent','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('source','llvm','base_native','parent'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);source=json.loads((a.source/'source.json').read_text())
    for name,digest in source['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared camera clock source differs: '+name)
    def run(command,label,cwd=None):
        r=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();scalar=['-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
        '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source]
    objects={};commands={}
    for name,filename in [('CameraClocks.o','Clock.c'),('CameraBoard.o','Board.c')]:
        obj=out/name;cmd=[a.llvm/'clang',*scalar,'-c',a.source/filename,'-o',obj];run(cmd,name+'-build');objects[name]=obj;commands[name]=list(map(str,cmd))
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Original native headers changed: '+name)
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    obj=out/'NativeClocks.o';cmd=[*flags,'-I',a.source,'-mgeneral-regs-only','-Werror',a.source/'NativeClocks.cpp','-o',obj]
    run(cmd,'NativeClocks.o-build',remap(job['directory']));objects[obj.name]=obj;commands[obj.name]=list(map(str,cmd))
    all_defined=set();all_imports=set()
    for name,obj in objects.items():
        asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('New native object must remain scalar: '+name)
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic camera clock/board init')
        symbols=global_symbols(obj);defined={n for n,v in symbols.items() if v[0]&0xe};imports={n for n,v in symbols.items() if not v[0]&0xe}
        if all_defined&defined:raise ValueError('Duplicate native provider')
        all_defined|=defined;all_imports|=imports
    available=set()
    for line in subprocess.check_output(['xcrun','nm','-a',a.parent/'native.macho'],text=True).splitlines():
        m=re.match(r'^[0-9a-fA-F]+\s+[sStTdDbB]\s+(\S+)$',line)
        if m:available.add(m[1])
    if all_imports-all_defined-available:raise ValueError('Unresolved real native provider: '+str(all_imports-all_defined-available))
    report={'passed':True,'native_objects':{n:{'sha256':sha(o),'imports':[n for n,v in global_symbols(o).items() if not v[0]&0xe]} for n,o in objects.items()},
        'source_record_sha256':sha(a.source/'source.json'),'compile_commands':commands,'native_cwd':remap(job['directory']),
        'compile_warnings':{n:len(re.findall(r'warning:',(out/(n+'-build.log')).read_text())) for n in objects},
        'actual_native_clock_board_WL_CCI_CAMSS_composition':True,'parent_kernel_sha256':sha(a.parent/'kernel.macho'),
        'physical_PLL_RPMh_GPIO_DMACameraOwner_installed':False,'full_link':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS three scalar OS17 camera clock/actual MMIO/board objects; native imports resolved, no automatic initialization')
if __name__=='__main__':main()
