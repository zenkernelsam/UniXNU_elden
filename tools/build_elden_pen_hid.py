"""Compile the qualified pen service and helpers against a source-built XNU context.
No prebuilt GPU/HID objects, kernel/boot installation or tablet access.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base-native','derived','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cap=json.loads((a.base_native/'capture.json').read_text());rec=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=rec['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,item in cap['files'].items():
        if sha(Path(remap(name)))!=item['sha256']:raise ValueError('Captured source/header differs')
    sources=json.loads((a.derived/'source.json').read_text())
    if not sources['qualified']:raise ValueError('Original HID ABI not qualified')
    for name in ('Owner.c','Owner.h','NativeBridge.c','NativeBridge.h'):
        if name not in sources['sources']:raise ValueError('Bridge source omitted from derived set: '+name)
    for name,digest in sources['sources'].items():
        if sha(a.derived/name)!=digest:raise ValueError('Derived input changed')
    def cpp_flags(job):
        flags=[];i=0
        while i<len(job['arguments']):
            v=job['arguments'][i]
            if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
            if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
            flags.append(remap(v));i+=1
        return flags+['-I',str(a.derived),'-I',remap(str(Path(job['file']).parent)),'-Werror','-mgeneral-regs-only']
    records={}
    def run(cmd,name,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; logs retained')
        return r.stdout.decode()
    for name in ('EldenPenHid.cpp','HidReport.c','PenReport.c','Delivery.c','Tilt.c','Owner.c','NativeBridge.c','EldenTouchRuntime.cpp'):
        src=a.derived/name;obj=out/(src.stem+'.o')
        if src.suffix=='.cpp':
            job=rec if name=='EldenPenHid.cpp' else next(j for j in cap['jobs'] if j['file'].endswith('/EldenNvt36536/Runtime.cpp'))
            raw=out/(src.stem+'.raw.o') if name=='EldenPenHid.cpp' else obj
            cmd=[*cpp_flags(job),str(src),'-o',str(raw)]
            if name=='EldenTouchRuntime.cpp':
                # Existing C++ aggregate {0} zero-initializes every remaining
                # field. Retain its one hash-bound baseline diagnostic; verify
                # the exact site below, never waive a new warning silently.
                cmd+=['-Wno-error=missing-field-initializers']
            run(cmd,src.stem+'-compile',remap(job['directory']))
            if name=='EldenPenHid.cpp':
                run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_EldenPenHid.cpp',raw,obj],src.stem+'-late')
                if sections(raw)['__TEXT/__text']!=sections(obj)['__TEXT/__text']:raise ValueError('Late init changed code')
        else:
            clang=a.llvm/'clang';resource=subprocess.check_output([str(clang),'-print-resource-dir'],text=True).strip()
            cmd=[str(clang),'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
                 '-nostdinc','-isystem',str(Path(resource)/'include'),'-Wall','-Wextra','-Werror','-c',str(src),'-o',str(obj)]
            run(cmd,src.stem+'-compile')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-disassembly')
        instructions=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not instructions or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in instructions):raise ValueError('Non-scalar code')
        sec=sections(obj)
        if any(k.endswith('/__mod_init_func') for k in sec):raise ValueError('Unexpected automatic initializer')
        symbols=global_symbols(obj)
        log=(out/(src.stem+'-compile.log')).read_text()
        warnings=log.count('warning:')
        if name=='EldenTouchRuntime.cpp':
            if warnings!=1 or not re.search(r"DmaSurvey\.h:52:32: warning: missing field 'slot' initializer \[-Wmissing-field-initializers\]",log):
                raise ValueError('Touch warning differs from the retained baseline')
        elif warnings:raise ValueError('New pen code must compile without warnings')
        records[name]={'sha256':sha(obj),'source_sha256':sha(src),'imports':[k for k,v in symbols.items() if not v[0]&0xe],
                       'exports':[k for k,v in symbols.items() if v[0]&0xe],'command':cmd,'warnings':warnings}
    run(['ar','rcs',out/'libEldenPen.a',*[out/(Path(n).stem+'.o') for n in records if n!='EldenTouchRuntime.cpp']],'archive')
    (out/'result.json').write_text(json.dumps({'compiled':True,'objects':records,'archive_sha256':sha(out/'libEldenPen.a'),
        'original_abi_source_sha256':sha(a.derived/'source.json'),'touch_hook_compiled':True,
        'separate_replacement_object':'EldenTouchRuntime.o replaces display archive EldenNvtRuntime.o; never add alongside it',
        'full_link':False,'hardware':False},indent=2)+'\n')
    print('PASS: native pen service and helpers; explicit late initializer, scalar, no automatic init; full link pending')
if __name__=='__main__':main()
