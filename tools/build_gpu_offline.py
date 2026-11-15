"""Prepare pinned GPU sources and qualify reusable components without device access.

prepare runs wherever Git is available. check runs with Clang on Linux/macOS.
The C transport/platform components use the real ARM64/iOS17 target;
the C++ initialization suite uses explicitly labelled host IOKit doubles.
The C++ native GPU driver, Metal and Turnip remain separate later stages.
"""
from pathlib import Path, PurePosixPath
import argparse, hashlib, io, json, os, re, struct, subprocess, tarfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'config/gpu-offline.lock.json'

def sha(data): return hashlib.sha256(data).hexdigest()
def audit_native(data):
    if len(data)<32 or struct.unpack_from('<4I',data)!=(0xfeedfacf,0x100000c,0,1):
        raise ValueError('Expected ordinary ARM64 Mach-O object')
    count,total=struct.unpack_from('<II',data,16);at=32;ios17=False
    if 32+total>len(data):raise ValueError('Truncated load commands')
    for _ in range(count):
        if at+8>32+total:raise ValueError('Truncated command')
        cmd,size=struct.unpack_from('<II',data,at)
        if size<8 or at+size>32+total:raise ValueError('Invalid command size')
        if cmd==0x32:
            if size<24:raise ValueError('Invalid build version')
            ios17=struct.unpack_from('<II',data,at+8)==(2,0x110000)
        at+=size
    if at!=32+total or not ios17:raise ValueError('Expected iOS17 object version')
def write_json(path, obj): path.write_text(json.dumps(obj, indent=2)+'\n', encoding='utf-8')
def run(args, log=None, **kw):
    result = subprocess.run([str(x) for x in args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180, **kw)
    if log: log.write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}), log={log}: {args[0]}\n'+result.stdout.decode('utf-8','replace')[-2500:])
    return result.stdout

def prepare(args):
    lock=json.loads(LOCK.read_text()); out=args.output.resolve(); out.mkdir(parents=True,exist_ok=False)
    manifest={}
    for name, spec in lock['repositories'].items():
        repo=args.references/spec['directory']
        commit=run(['git','-C',repo,'rev-parse',spec['commit']+'^{commit}']).decode().strip()
        if commit!=spec['commit']: raise ValueError('Unresolved exact pin: '+name)
        # Read the object database, never change HEAD, references, files or submodules.
        archive=run(['git','-C',repo,'archive','--format=tar',commit,*spec.get('paths',[])])
        dest=out/'sources'/name; dest.mkdir(parents=True)
        files={}
        with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
            for entry in tf:
                rel=PurePosixPath(entry.name)
                if rel.is_absolute() or '..' in rel.parts or entry.issym() or entry.islnk():
                    raise ValueError('Unsafe source archive member: '+entry.name)
                if not entry.isfile(): continue
                # Only source/document inputs, no prebuilt firmware or executables.
                if rel.suffix.lower() not in ('.c','.cpp','.h','.inc','.md','.json','.toml','.py','.dts','.dtsi','.patch','.mm','.rs') and rel.name not in ('LICENSE','NOTICE','COPYING'): continue
                data=tf.extractfile(entry).read(); target=dest/entry.name
                target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
                files[entry.name]=sha(data)
        if not files: raise ValueError('Empty source selection: '+name)
        manifest[name]={'commit':commit,'url':spec['url'],'files':files}
    write_json(out/'source-manifest.json',manifest)
    write_json(out/'preparation.json',{'lock_sha256':sha(LOCK.read_bytes()),'source_counts':{k:len(v['files']) for k,v in manifest.items()},'device_access':False})
    print('Prepared exact Git source exports; no reference checkout or device changed.')

def check_sources(out):
    lock=json.loads(LOCK.read_text()); preparation=json.loads((out/'preparation.json').read_text())
    if preparation['lock_sha256']!=sha(LOCK.read_bytes()): raise ValueError('Lock changed after preparation')
    manifest=json.loads((out/'source-manifest.json').read_text())
    if set(manifest)!=set(lock['repositories']): raise ValueError('Source set mismatch')
    for name, spec in manifest.items():
        if spec['commit']!=lock['repositories'][name]['commit']: raise ValueError('Commit mismatch')
        for rel,digest in spec['files'].items():
            if sha((out/'sources'/name/rel).read_bytes())!=digest: raise ValueError('Modified input: '+name+'/'+rel)
    return manifest

def check(args):
    out=args.output.resolve(); manifest=check_sources(out)
    build=out/'checks'; build.mkdir(exist_ok=False)
    src=out/'sources'; gpu=src/'gpu'; fw=src/'firmware'; pm4=src/'pm4'
    soc=src/'soc/kits/Sm8750Platform'; smmu=src/'smmu'
    cc=args.cc; cxx=args.cxx; includes=[x for p in (soc,fw,pm4,smmu,gpu) for x in ('-I',str(p))]
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-D_POSIX_C_SOURCE=200112L','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if os.uname().sysname=='Linux': flags+=['-fno-pie','-no-pie']
    c_sources=[soc/(n+'.c') for n in ('Sm8750Gmu','Sm8750GmuCommandDb','Sm8750GpuPower','Sm8750GpuZap','Sm8750SocInfo','Sm8750GpuSmmu')]
    c_sources += [fw/(n+'.c') for n in ('AdrenoFirmware','AdrenoHfi','AdrenoHfiStartup','AdrenoHfiVotes')]
    objects=[]; commands=[]
    def invoke(cmd,label):
        commands.append([str(x) for x in cmd]);return run(cmd,build/(label+'.log'))
    # GPU SMMU only gets native compilation below: it needs its full ArmSmmu4K peer to link.
    for source in c_sources:
        if source.stem=='Sm8750GpuSmmu':continue
        obj=build/(source.stem+'.host.o')
        objflags=[x for x in flags if x!='-no-pie']
        invoke([cc,'-std=c11',*objflags,*includes,'-DSM8750_GMU_HOST_TEST','-DADRENO_HFI_HOST_TEST','-c',source,'-o',obj],source.stem+'-host')
        objects.append(obj)
    tests=[('power',soc/'Test/gpu-power.c',objects),
           ('zap',soc/'Test/gpu-zap.c',[build/'Sm8750GpuZap.host.o']),
           ('socinfo',soc/'Test/socinfo.c',[build/'Sm8750SocInfo.host.o']),
           ('hfi',fw/'test_hfi.c',[build/'AdrenoHfi.host.o']),
           ('hfi-startup',fw/'test_startup.c',[build/'AdrenoHfi.host.o',build/'AdrenoHfiStartup.host.o']),
           ('pm4',pm4/'test_pm4.c',[pm4/'AdrenoPM4.c'])]
    results={}
    for name,source,objs in tests:
        binary=build/name
        invoke([cc,'-std=c11',*flags,*includes,'-DSM8750_GMU_HOST_TEST','-DADRENO_HFI_HOST_TEST',source,*objs,'-o',binary],name+'-link')
        output=invoke([binary],name+'-run').decode()
        if 'PASS' not in output:raise ValueError('Missing upstream PASS: '+name)
        results[name]=output.strip()
        print(name+': passed',flush=True)
    host_headers=build/'host-headers/IOKit';host_headers.mkdir(parents=True)
    for name in ('IOTypes.h','IOLib.h'):(host_headers/name).write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')
    pm4obj=build/'AdrenoPM4.host.o'
    invoke([cc,'-std=c11',*[x for x in flags if x!='-no-pie'],*includes,'-c',pm4/'AdrenoPM4.c','-o',pm4obj],'pm4-host')
    cpp=[gpu/(n+'.cpp') for n in ('NativeAdrenoRing','NativeAdrenoCpInit','NativeAdrenoPwrup','NativeAdrenoA830HwInit','NativeAdrenoPreempt')]
    binary=build/'a830-hwinit'
    invoke([cxx,'-std=c++17',*flags,*includes,'-I',gpu/'tests','-I',host_headers.parent,
            '-include',gpu/'tests/XnuMemoryFixture.h',gpu/'tests/test_a830_hwinit.cpp',*cpp,pm4obj,'-o',binary],'a830-link')
    output=invoke([binary],'a830-run').decode()
    if 'PASS: A830 protected initialization' not in output:raise ValueError('Missing A830 test acceptance')
    results['a830-hwinit']=output.strip()
    # Real target, no host fixture, sanitizer or host-test feature flags.
    resource=run([cc,'-print-resource-dir']).decode().strip()
    native=build/'native';native.mkdir();native_records={}
    for source in c_sources+[pm4/'AdrenoPM4.c']:
        obj=native/(source.stem+'.o')
        invoke([cc,'-target','arm64-apple-ios17.0','-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
                '-nostdinc','-isystem',Path(resource)/'include',*includes,'-c',source,'-o',obj],source.stem+'-native')
        audit_native(obj.read_bytes())
        asm=invoke([args.objdump,'--disassemble','--no-show-raw-insn',obj],source.stem+'-asm').decode()
        # Apple ARM64 instructions must not introduce floating-point/SIMD or initializers.
        if re.search(r'^\s*[0-9a-f]+:.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M):raise ValueError('SIMD register: '+source.stem)
        sections=invoke([args.objdump,'--section-headers',obj],source.stem+'-sections').decode()
        if '__mod_init_func' in sections:raise ValueError('Unexpected initializer')
        symbols=invoke([args.nm,'--undefined-only',obj],source.stem+'-undefined').decode().strip()
        native_records[source.stem]={'sha256':sha(obj.read_bytes()),'undefined':symbols.splitlines(),'bytes':obj.stat().st_size}
    # Recheck inputs after all compilations; firmware bytes and generated boot
    # components are intentionally outside this offline package.
    check_sources(out)
    report={'passed':True,'scope':'7 upstream host suites and 11 native C component objects; no C++ native driver/link/boot',
            'source_manifest_sha256':sha((out/'source-manifest.json').read_bytes()),'toolchain':run([cc,'--version']).decode(),
            'host_tests':results,'native_target':'arm64-apple-ios17.0','native_objects':native_records,
            'native_host_fixtures':False,'native_unresolved_imports_review_required':True,
            'board_adapted':False,'firmware_executed':False,'device_access':False,'whole_kernel':False,
            'metal_turnip_built':False,'commands':commands}
    report['unrun_suites']={'test_votes.c':'Requires the pinned external Linux source_arc/source_bandwidth oracle. Prior link failure retained; no arithmetic differential-test pass claimed.'}
    write_json(build/'result.json',report)
    print(json.dumps({'passed':True,'host_suites':len(results),'native_objects':len(native_records),'device_access':False}))

def main():
    global LOCK
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['prepare','check'])
    p.add_argument('--lock',type=Path,default=LOCK,help='Reviewed source lock; permits isolated update proposals')
    p.add_argument('--output',type=Path,required=True);p.add_argument('--references',type=Path)
    p.add_argument('--cc',default='clang');p.add_argument('--cxx',default='clang++')
    p.add_argument('--objdump',default='llvm-objdump');p.add_argument('--nm',default='llvm-nm')
    args=p.parse_args()
    LOCK=args.lock.resolve()
    if args.action=='prepare':
        if args.references is None:p.error('prepare requires --references')
        prepare(args)
    else:check(args)
if __name__=='__main__':main()
