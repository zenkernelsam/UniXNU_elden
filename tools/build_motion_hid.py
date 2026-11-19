"""Compile real OS17 accel/gyro HID source with exact XNU headers, no producer."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','base-native','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in ('source','base_native','llvm'):setattr(a,key,getattr(a,key).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);r=json.loads((a.source/'source.json').read_text())
    for n,d in r['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Source changed')
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,
        a.source/'Hid.c',DRIVER/'TestHid.c','-o',out/'test'],'host-build');host=run([out/'test'],'host-run')
    host_flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run(['xcrun','clang','-std=c11',*host_flags,'-c',a.source/'Hid.c','-o',out/'hid-host.o'],'hid-host')
    run(['xcrun','clang++','-std=c++17',*host_flags,DRIVER/'TestNativeHid.cpp',out/'hid-host.o','-o',out/'native-test'],'native-host-build')
    native_host=run([out/'native-test'],'native-host-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Captured source differs')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    raw=out/'EldenMotionHid.raw.o';obj=out/'EldenMotionHid.o';cmd=[*flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'EldenMotionHid.cpp','-o',raw]
    run(cmd,'native-build',remap(job['directory']))
    run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_EldenMotionHid.cpp',raw,obj],'late-init')
    if sections(raw)['__TEXT/__text']!=sections(obj)['__TEXT/__text']:raise ValueError('Late initialization changed code')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();hid=out/'MotionHid.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
        '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Hid.c','-o',hid],'hid-native')
    objects={}
    for p in [obj,hid]:
        d=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',p],p.stem+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',d,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
        if any(k.endswith('/__mod_init_func') for k in sections(p)):raise ValueError('Automatic motion initialization')
        objects[p.name]={'sha256':sha(p),'imports':[n for n,v in global_symbols(p).items() if not v[0]&0xe]}
    run(['ar','rcs',out/'libEldenMotionHid.a',obj,hid],'archive')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':host,'objects':objects,'archive_sha256':sha(out/'libEldenMotionHid.a'),
        'source_record_sha256':sha(a.source/'source.json'),'actual_Gen4_producer_connected':False,'device_orientation_service':False,
        'actual_native_HID_methods_host_tested':True,'native_host_output':native_host,'OS_HID_methods_modeled':True,
        'full_link':False,'hardware':False,'native_command':list(map(str,cmd))},indent=2)+'\n');print(host+native_host)
if __name__=='__main__':main()
