"""Compile source-bound shared-backlight and WL2868C native objects for OS17."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,struct,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def symbols(p,defined):return {n for n,v in global_symbols(p).items() if bool(v[0]&0xe)==defined}

def check_backlight_abi(obj,original):
    """Read actual object relocations, preserving 169 slots/10 overrides."""
    def table(path):
        raw=path.read_bytes();at=32;secs=[];sy=None
        for _ in range(struct.unpack_from('<I',raw,16)[0]):
            cmd,size=struct.unpack_from('<II',raw,at)
            if cmd==2:sy=struct.unpack_from('<4I',raw,at+8)
            if cmd==0x19:
                for i in range(struct.unpack_from('<I',raw,at+64)[0]):
                    x=at+72+80*i;va,length,fileoff=struct.unpack_from('<QQI',raw,x+32);roff,nrel=struct.unpack_from('<II',raw,x+56)
                    secs.append((va,length,fileoff,roff,nrel))
            at+=size
        if sy is None:raise ValueError('No actual object symbol table')
        offset,count,strings,_=sy;names=[];values={}
        for i in range(count):
            ix,kind,section,_,va=struct.unpack_from('<IBBHQ',raw,offset+16*i)
            name=raw[strings+ix:raw.index(b'\0',strings+ix)].decode();names.append(name)
            if not kind&0xe0:values[name]=va
        vt=values['__ZTV14EldenBacklight'];sec=next(s for s in secs if s[0]<=vt<s[0]+s[1]);relocs={}
        for i in range(sec[4]):
            off,bits=struct.unpack_from('<II',raw,sec[3]+8*i)
            if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocs[sec[0]+off]=names[bits&0xffffff]
        slots=[relocs.get(vt+16+8*i) for i in range(169)]
        if not all(slots) or vt+16+169*8 in relocs:raise ValueError('Backlight vtable extent changed')
        own=[i for i,n in enumerate(slots) if n.startswith(('__ZN14EldenBacklight','__ZNK14EldenBacklight'))]
        if own!=[0,1,7,14,21,44,86,87,111,168]:raise ValueError('Backlight overrides changed')
        return slots
    if table(obj)!=table(original):raise ValueError('Actual inherited backlight slots differ')
    return {'actual_slots':169,'overrides':[0,1,7,14,21,44,86,87,111,168],'all_original_slot_targets_equal':True}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','base-native','parent','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('source','llvm','base_native','parent'):setattr(a,name,getattr(a,name).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);source=read(a.source/'source.json')
    for name,digest in source['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared source changed: '+name)
    if source['backlight_variant']!='pd-power':raise ValueError('This parent requires original PD/boot backlight variant')
    cap=read(a.base_native/'capture.json');project=a.base_native/'build/project'
    job=next(j for j in cap['jobs'] if j['file'].endswith('/backlight-elden-20261109/source-input-v13/BacklightRuntime.cpp'))
    historical=job['file'].split('/research/',1)[0]
    def remap(v):return re.sub(re.escape(historical)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Fixed native dependency changed: '+name)
    cmd=read(a.parent/'link-command.json');ars=[Path(v.split(',')[-1]) for v in cmd if v.endswith('/libEldenBacklight.a')]
    if len(ars)!=1:raise ValueError('Exactly one original backlight controller required')
    oldar=ars[0];oldfolder=out/'original-members';oldfolder.mkdir();original={}
    members=[n for n in subprocess.check_output(['ar','t',oldar],text=True).splitlines() if not n.startswith('__.SYMDEF')]
    for name in members:
        body=subprocess.check_output(['ar','p',oldar,name]);(oldfolder/name).write_bytes(body);original[name]=sha(oldfolder/name)
    if set(members)!={'GeniI2c.o','Aw99706.o','Platform.o','BacklightRuntime.late.o','original-bindings.o'}:raise ValueError('Original backlight members changed')
    def run(command,label,cwd=None):
        r=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,timeout=120);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; log retained')
        return r.stdout.decode()
    # Captured genuine Backlight headers/command retain the actual original
    # instance size and late metaclass initialization. No custom new ABI.
    cxx=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        cxx.append(remap(v));i+=1
    dependency=ROOT/'drivers/EldenPmicGlink/PdObservation.h'
    shutil.copyfile(dependency,out/'PdObservation.h')
    runtime=out/'BacklightRuntime.o';command=[*cxx,'-I',a.source,'-I',out,'-Werror','-mgeneral-regs-only',a.source/'BacklightRuntime.cpp','-o',runtime]
    run(command,'shared-native',remap(job['directory']))
    late=out/'BacklightRuntime.late.o';run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func',
         '--globalize-symbol=__GLOBAL__sub_I_BacklightRuntime.cpp',runtime,late],'defer-class-init')
    if sections(runtime).get('__TEXT/__text')!=sections(late).get('__TEXT/__text'):raise ValueError('Late class transform changed instructions')
    abi=check_backlight_abi(late,oldfolder/'BacklightRuntime.late.o')
    if symbols(late,True)!=symbols(oldfolder/'BacklightRuntime.late.o',True)|{'_elden_camera_i2c10_'+n for n in ('open','read','write','close')}:
        raise ValueError('Shared backlight defined API changed')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();scalar=['-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding',
        '-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source]
    objects={'BacklightRuntime.late.o':late};commands={'BacklightRuntime.late.o':list(map(str,command))}
    for filename,name in [('Platform.c','Platform.o'),('Power.c','CameraPower.o'),('PowerBus.c','CameraPowerBus.o'),('PowerBinding.c','CameraPowerBinding.o')]:
        obj=out/name;cc=[a.llvm/'clang',*scalar,'-c',a.source/filename,'-o',obj];run(cc,name+'-native');objects[name]=obj;commands[name]=list(map(str,cc))
    if symbols(objects['Platform.o'],True)!=symbols(oldfolder/'Platform.o',True)|{'_elden_bl_platform_i2c10_read','_elden_bl_platform_i2c10_write'}:
        raise ValueError('Original platform API changed')
    for name,obj in objects.items():
        asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('New native object must be scalar: '+name)
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('New automatic startup: '+name)
    archivefolder=out/'backlight';archivefolder.mkdir();after={}
    for name in members:
        body=objects[name].read_bytes() if name in objects else (oldfolder/name).read_bytes()
        (archivefolder/name).write_bytes(body);after[name]=sha(archivefolder/name)
    archive=archivefolder/'libEldenBacklight.a';run([a.llvm/'llvm-ar','rcsD',archive,*[archivefolder/n for n in members]],'shared-archive')
    for name in members:
        actual=subprocess.check_output(['ar','p',archive,name]);expected=(archivefolder/name).read_bytes()
        if actual!=expected and actual!=expected+b'\n'*((-len(expected))%8):raise ValueError('Actual archive member changed: '+name)
        after[name]=hashlib.sha256(actual).hexdigest()
        if name not in objects and original[name]!=after[name]:raise ValueError('Unrelated backlight member replaced')
    imports={n for obj in objects.values() for n in symbols(obj,False)};providers={n for obj in objects.values() for n in symbols(obj,True)}
    # XNU's hidden runtime handlers are real linked local definitions, omitted
    # by the exported-symbol reader. Qualify them through the actual symbol
    # table as well; no stub or fallback runtime is added.
    available=symbols(a.parent/'native.macho',True)
    for line in subprocess.check_output(['xcrun','nm','-a',a.parent/'native.macho'],text=True).splitlines():
        match=re.match(r'^[0-9a-fA-F]+\s+[sStTdDbB]\s+(\S+)$',line)
        if match:available.add(match[1])
    if imports-providers-available:raise ValueError('Native import missing: '+str(imports-providers-available))
    report={'passed':True,'source_record_sha256':sha(a.source/'source.json'),'shared_archive_sha256':sha(archive),'parent_shared_archive':str(oldar),
        'parent_shared_archive_sha256':sha(oldar),'members':{n:{'before':original[n],'after':after[n]} for n in members},'backlight_ABI':abi,
        'native_objects':{n:{'sha256':sha(o),'imports':sorted(symbols(o,False))} for n,o in objects.items()},'compile_commands':commands,
        'native_cwd':remap(job['directory']),'extra_dependencies':{'drivers/EldenPmicGlink/PdObservation.h':sha(dependency)},
        'shared_I2c10_only_one_controller':True,'actual_WL_power_session':True,
        'native_compile_warnings':{name:len(re.findall(r'warning:',(out/label).read_text())) for name,label in
            [('BacklightRuntime.late.o','shared-native.log'),('Platform.o','Platform.o-native.log'),('CameraPower.o','CameraPower.o-native.log'),('CameraPowerBus.o','CameraPowerBus.o-native.log'),('CameraPowerBinding.o','CameraPowerBinding.o-native.log')]},
        'hardware':False,'auto_start':False,'full_link':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS scalar OS17 shared I2C10/WL2868C objects, genuine169slot backlight/late init and original GENI/AW/bindings retained')
if __name__=='__main__':main()
