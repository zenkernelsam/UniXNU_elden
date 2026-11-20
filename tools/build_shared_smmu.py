"""Build scalar sharedSMMU/MMIO/GIC and original staged memory owners for OS17."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,shutil
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','camera-source','sensor-source','runtime-source','base-native','llvm','parent','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('source','camera_source','sensor_source','runtime_source','base_native','llvm','parent'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);records={}
    for n in ('source','camera_source','sensor_source','runtime_source'):
        folder=getattr(a,n);record=json.loads((folder/'source.json').read_text());records[n]=sha(folder/'source.json')
        for name,digest in record['sources'].items():
            if sha(folder/name)!=digest:raise ValueError('Qualified actual source changed: '+name)
    def run(command,label,cwd=None):
        r=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Qualified original native dependency changed: '+name)
    flags=[];i=0
    while i<len(job['arguments']):
        value=job['arguments'][i]
        if value in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if value==job['file'] or value in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(value));i+=1
    # The real SSC runtime stores memory_owner by value. Rebuild its actual
    # type-layout consumer against the extended owner, keeping its body exact.
    rt=out/'runtime-source';rt.mkdir();rr=json.loads((a.runtime_source/'source.json').read_text())
    for name in rr['sources']:shutil.copyfile(a.runtime_source/name,rt/name)
    shutil.copyfile(a.sensor_source/'NativeMemory.h',rt/'NativeMemory.h')
    for name in ('SensorRuntime.h','SensorRuntimeMethods.inc'):shutil.copyfile(ROOT/'drivers/EldenMotion'/name,rt/name)
    runtime_record={'parent':sha(a.runtime_source/'source.json'),'new_memory_owner_header':sha(rt/'NativeMemory.h'),'actual_adopt_prepared_native_memory':True,
                    'sources':{f.name:sha(f) for f in rt.iterdir() if f.is_file()}}
    (rt/'source.json').write_text(json.dumps(runtime_record,indent=2)+'\n')
    commands={};objects={};resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    sources={'Shared.o':a.source/'Shared.c','SharedBootRoute.o':a.source/'BootRoute.c','SharedPeripheral.o':a.source/'Peripheral.c','SharedTouchRoute.o':a.source/'DmaRoute.c'}
    cflags=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c']
    for name,source in sources.items():
        obj=out/name;cmd=[*cflags,source,'-o',obj];run(cmd,name+'-build');commands[name]=list(map(str,cmd));objects[name]=obj
    for name,folder,source in [('SharedNative.o',a.source,'SharedNative.cpp'),('NativeCameraDma.o',a.camera_source,'NativeDma.cpp'),
                              ('NativeMemory.o',a.sensor_source,'NativeMemory.cpp'),('SensorRuntimeNative.o',rt,'SensorRuntimeNative.cpp')]:
        obj=out/name;cmd=[*flags,'-I',folder,'-Werror','-mgeneral-regs-only',folder/source,'-o',obj]
        run(cmd,name+'-build',remap(job['directory']));commands[name]=list(map(str,cmd));objects[name]=obj
    defined=set();imports=set();proof={}
    for name,obj in objects.items():
        asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('New shared native object must remain scalar')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic shared/peripheral initialization')
        symbols=global_symbols(obj);exports={n for n,v in symbols.items() if v[0]&0xe};used={n for n,v in symbols.items() if not v[0]&0xe}
        if exports&defined:raise ValueError('Duplicate shared native definition')
        defined|=exports;imports|=used;proof[name]={'sha256':sha(obj),'imports':sorted(used),'exports':sorted(exports),
            'warnings':(out/(name+'-build.log')).read_text().count('warning:')}
    available=set()
    for line in subprocess.check_output(['xcrun','nm','-a',a.parent/'native.macho'],text=True).splitlines():
        m=re.match(r'^[0-9a-fA-F]+\s+[sStTdDbB]\s+(\S+)$',line)
        if m:available.add(m[1])
    if imports-defined-available:raise ValueError('Actual native providers unresolved: '+str(imports-defined-available))
    report={'passed':True,'objects':proof,'compile_commands':commands,'native_cwd':remap(job['directory']),'source_records':records,
        'runtime_layout_source_record_sha256':sha(rt/'source.json'),'parent_kernel_sha256':sha(a.parent/'kernel.macho'),
        'scalar':True,'automatic_startup':False,'actual_MMIO_GIC_peripheral_provider':True,'physical_authority_installed':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS eight scalar OS17 sharedSMMU/native/staged-memory/layout-consumer objects; original4K reused once')
if __name__=='__main__':main()
