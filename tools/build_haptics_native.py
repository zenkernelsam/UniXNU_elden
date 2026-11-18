"""Compile the actual XNU haptics service, worker and bounded driver chain."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','base-native','touch-build','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('source','base_native','touch_build','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);cap=json.loads((a.base_native/'capture.json').read_text());r=json.loads((a.source/'source.json').read_text())
    for name,digest in r['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Derived input changed')
    job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'));old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
    remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for name,row in cap['files'].items():
        if sha(Path(remap(name)))!=row['sha256']:raise ValueError('Captured source/header differs')
    def cpp_flags(job):
        flags=[];i=0
        while i<len(job['arguments']):
            v=job['arguments'][i]
            if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
            if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
            flags.append(remap(v));i+=1
        return flags
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();records={}
    cflags=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
            '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror']
    for folder,name in [('haptics','EldenHapticsNative.cpp'),('haptics','Platform.c'),('haptics','GeniCore.c'),('haptics','Bus.c'),
                        ('haptics','AwChip.c'),('haptics','Queue.c'),('haptics','Mailbox.c'),('touch','EldenTouchRuntime.cpp')]:
        src=a.source/folder/name;obj=out/(src.stem+'.o');raw=out/(src.stem+'.raw.o') if name=='EldenHapticsNative.cpp' else obj
        if src.suffix=='.cpp':
            selected=job if folder=='haptics' else next(j for j in cap['jobs'] if j['file'].endswith('/EldenNvt36536/Runtime.cpp'))
            cmd=[*cpp_flags(selected),'-I',str(src.parent),'-Werror','-mgeneral-regs-only',str(src),'-o',str(raw)]
            if folder=='touch':cmd+=['-Wno-error=missing-field-initializers']
            run(cmd,src.stem+'-compile',remap(selected['directory']))
            if name=='EldenHapticsNative.cpp':
                run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_EldenHapticsNative.cpp',raw,obj],'late-initializer')
                if sections(raw)['__TEXT/__text']!=sections(obj)['__TEXT/__text']:raise ValueError('Late initialization changed code')
        else:
            cmd=[*cflags,'-I',src.parent,'-c',src,'-o',obj];run(cmd,src.stem+'-compile')
        log=(out/(src.stem+'-compile.log')).read_text();warnings=log.count('warning:')
        if folder=='touch':
            if warnings!=1 or not re.search(r"DmaSurvey\.h:52:32: warning: missing field 'slot' initializer \[-Wmissing-field-initializers\]",log):raise ValueError('Touch baseline warning differs')
        elif warnings:raise ValueError('New native warnings refused')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('SIMD/FP code refused')
        sec=sections(obj)
        if any(k.endswith('/__mod_init_func') for k in sec):raise ValueError('Automatic initializer refused')
        symbols=global_symbols(obj);records[name]={'source_sha256':sha(src),'object_sha256':sha(obj),'warnings':warnings,
            'imports':[n for n,v in symbols.items() if not v[0]&0xe],'exports':[n for n,v in symbols.items() if v[0]&0xe],
            'command':list(map(str,cmd)),'sections':{k:{'bytes':v[0],'flags':v[1],'sha256':v[2]} for k,v in sec.items()}}
    touch=json.loads((a.touch_build/'result.json').read_text());toc=a.touch_build/'EldenNvtPlatform.o'
    if not touch['passed'] or sha(toc)!=touch['object_sha256'] or touch['sources']['drivers/EldenNvt36536/Platform.c']!=sha(ROOT/'drivers/EldenNvt36536/Platform.c'):
        raise ValueError('Shared touch platform source/object differs')
    (out/'EldenNvtPlatform.o').write_bytes(toc.read_bytes())
    members=[out/(Path(n).stem+'.o') for n in records if n!='EldenTouchRuntime.cpp']
    run(['ar','rcs',out/'libEldenHaptics.a',*members],'archive')
    (out/'result.json').write_text(json.dumps({'compiled':True,'objects':records,'archive_sha256':sha(out/'libEldenHaptics.a'),
        'touch_platform_sha256':sha(toc),'touch_platform_report_sha256':sha(a.touch_build/'result.json'),
        'source_record_sha256':sha(a.source/'source.json'),'calibration_present':r['calibration'] is not None,
        'full_link':False,'hardware':False,'separate_replacements':['EldenNvtPlatform.o','EldenNvtRuntime.o']},indent=2)+'\n')
    print('PASS: native service/worker and actual driver chain; explicit late initialization; calibration '+('external' if r['calibration'] else 'disabled'))
if __name__=='__main__':main()
