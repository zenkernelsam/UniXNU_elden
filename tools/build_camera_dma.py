"""Build scalar native camera allocator and guarded frame/capture binding."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','base-native','parent','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('source','llvm','base_native','parent'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Actual prepared DMA source changed: '+name)
    def run(command,label,cwd=None):
        r=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Qualified native dependency changed')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    native=out/'NativeCameraDma.o';cmd=[*flags,'-I',a.source,'-mgeneral-regs-only','-Werror',a.source/'NativeDma.cpp','-o',native]
    run(cmd,'NativeCameraDma-build',remap(job['directory']));objects={native.name:native};commands={native.name:list(map(str,cmd))}
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();binding=out/'CameraDmaBinding.o'
    cc=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'DmaBinding.c','-o',binding]
    run(cc,'CameraDmaBinding-build');objects[binding.name]=binding;commands[binding.name]=list(map(str,cc))
    imports=set();defined=set()
    for name,obj in objects.items():
        asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('New native DMA must remain scalar')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic camera DMA startup')
        syms=global_symbols(obj);exports={n for n,v in syms.items() if v[0]&0xe};used={n for n,v in syms.items() if not v[0]&0xe}
        if exports&defined:raise ValueError('Duplicate native DMA definition')
        defined|=exports;imports|=used
    available=set()
    for line in subprocess.check_output(['xcrun','nm','-a',a.parent/'native.macho'],text=True).splitlines():
        m=re.match(r'^[0-9a-fA-F]+\s+[sStTdDbB]\s+(\S+)$',line)
        if m:available.add(m[1])
    if imports-defined-available:raise ValueError('Actual native provider unresolved: '+str(imports-defined-available))
    report={'passed':True,'native_objects':{n:{'sha256':sha(o),'imports':[n for n,v in global_symbols(o).items() if not v[0]&0xe]} for n,o in objects.items()},
        'compile_commands':commands,'native_cwd':remap(job['directory']),'source_record_sha256':sha(a.source/'source.json'),
        'compile_warnings':{n:len(re.findall(r'warning:',(out/(Path(n).stem+'-build.log')).read_text())) for n in objects},
        'original4K_algorithms_reused_once':True,'actual_native_allocator_PARCACHE_capture':True,
        'parent_kernel_sha256':sha(a.parent/'kernel.macho'),'hardware':False,'actual_earlySMMU_provider_installed':False,'full_link':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS two scalar native OS17 guarded camera DMA/actual capture objects; original4K imported once and no auto-init')
if __name__=='__main__':main()
