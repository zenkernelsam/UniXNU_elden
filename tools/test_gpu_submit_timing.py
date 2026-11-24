"""Host-test the derived NativeAdrenoSubmit timing port; no device IO.

Compiles TestSubmitTiming.cpp over the software register peers with the derived
submit pair and the pinned tree's real Ring/Vm/Memory/SmmuMemory units, then
runs it under ASan/UBSan. Also emits an ARM64 freestanding object of the derived
unit to prove it stays scalar and freestanding-clean. Host/model evidence only.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenGpu'
sys.path.insert(0,str(DRIVER));from SubmitSource import derive as submit_derive

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd,log):
    proc=subprocess.run(list(map(str,cmd)),capture_output=True)
    Path(log).write_bytes(proc.stdout+proc.stderr)
    return proc.returncode
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('tree','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--derived',type=Path,help='Directory already holding the derived pair; else it is derived here from --tree')
    p.add_argument('--llvm',type=Path,help='LLVM toolchain bin dir; default xcrun')
    p.add_argument('--base-native',type=Path,help='Native capture base (capture.json + build/project records) for the optional ARM64 object step')
    a=p.parse_args()
    tree=a.tree.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    gpu=tree/'drivers/Adreno740'
    derived=a.derived.resolve() if a.derived else out/'derived'
    if not a.derived:
        derived.mkdir()
        pin=json.loads((DRIVER/'upstream/submit-source.json').read_text())
        cpp_src=(gpu/'NativeAdrenoSubmit.cpp').read_bytes();h_src=(gpu/'NativeAdrenoSubmit.h').read_bytes()
        for name,data in (('NativeAdrenoSubmit.cpp',cpp_src),('NativeAdrenoSubmit.h',h_src)):
            if hashlib.sha256(data).hexdigest()!=pin['files'][name]:raise ValueError('Submit source differs from the derivation pin: '+name)
        cpp,h=submit_derive(cpp_src,h_src)
        (derived/'NativeAdrenoSubmit.cpp').write_bytes(cpp);(derived/'NativeAdrenoSubmit.h').write_bytes(h)
    for name in ('NativeAdrenoSubmit.cpp','NativeAdrenoSubmit.h'):
        if not (derived/name).is_file():raise ValueError('Derived pair incomplete: '+name)
    llvm=a.llvm
    cxx=[str(llvm/'clang++')] if llvm else ['xcrun','clang++']
    cc=[str(llvm/'clang')] if llvm else ['xcrun','clang']
    kits=tree/'kits'
    # IOKit/kern stubs: the upstream XnuMemoryFixture supplies the doubles.
    stubs=out/'host-headers'
    for rel in ('IOKit/IOTypes.h','IOKit/IOLib.h','IOKit/IOBufferMemoryDescriptor.h','kern/task.h'):
        f=stubs/rel;f.parent.mkdir(parents=True,exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')
    includes=['-I',derived,'-I',gpu,'-I',gpu/'tests','-I',stubs,'-I',kits/'ArmSmmu4K','-I',kits/'AdrenoPM4',
              '-I',kits/'AdrenoFirmware','-I',kits/'Sm8750Platform']
    flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
           '-DNATIVE_ADRENO_HOST_TEST','-include',gpu/'tests/XnuMemoryFixture.h',*includes]
    units=[derived/'NativeAdrenoSubmit.cpp',gpu/'NativeAdrenoRing.cpp',gpu/'NativeAdrenoVm.cpp',
           gpu/'NativeAdrenoMemory.cpp',gpu/'NativeAdrenoSmmuMemory.cpp',gpu/'NativeAdrenoPreempt.cpp']
    # ArmSmmu4KMmio.c is the direct SMMUv2 MMIO binding and compiles only for little-endian
    # AArch64; the host suite drives the software register peers instead.
    kits_c=sorted(p for p in (kits/'ArmSmmu4K').glob('ArmSmmu4K*.c') if p.name!='ArmSmmu4KMmio.c')+ \
        [kits/'AdrenoPM4'/'AdrenoPM4.c',kits/'AdrenoPM4'/'AdrenoConstants.c']
    kit_objs=[]
    for i,src in enumerate(kits_c):
        obj=out/('kit%d.o'%i)
        kc=[*cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I',kits/'ArmSmmu4K','-I',kits/'AdrenoPM4','-c',src,'-o',obj]
        krc=run(kc,out/('kit%d.log'%i))
        if krc:raise SystemExit('Kit compile failed: '+src.name)
        kit_objs.append(obj)
    binary=out/'test-submit-timing'
    cmd=[*cxx,*flags,DRIVER/'TestSubmitTiming.cpp',*units,*kit_objs,'-o',binary]
    record={'command':list(map(str,cmd))}
    rc=run(cmd,out/'build.log');record['build_rc']=rc
    if rc:raise SystemExit('Compile failed; see '+str(out/'build.log'))
    # The fixture's SMMU/memory allocations live for the process by design (same as the upstream
    # harness); leak detection would only report that shape, so it is off for the run step.
    env=dict(os.environ);env['ASAN_OPTIONS']=(env.get('ASAN_OPTIONS','')+':detect_leaks=0').lstrip(':')
    proc=subprocess.run([binary],capture_output=True,env=env)
    (out/'run.log').write_bytes(proc.stdout+proc.stderr);rc=proc.returncode;record['run_rc']=rc
    text=(out/'run.log').read_text(errors='replace')
    record['pass']='PASS' in text and rc==0
    # Optional ARM64 object of the derived unit over a native build's exported kernel headers
    # (the standalone suite has no IOKit headers of its own); the authoritative native compile
    # remains build_gpu_native.py --submit.
    obj=out/'NativeAdrenoSubmit.o'
    if a.base_native:
        # The standalone ARM64 object check replays the real C++ kernel context captured
        # in the native base, the same replay build_gpu_native.py performs: captured flags
        # (sanitizer, -nostdlibinc, fakeroot includes, meta_features.h, DEVELOPMENT cwd)
        # with source-path arguments remapped onto the staged records tree.
        base=a.base_native.resolve()
        cap=json.loads((base/'capture.json').read_text())
        ms=[j for j in cap['jobs'] if j['file'].endswith('/NativeLegacyDisplay.cpp')]
        if len(ms)!=1:raise SystemExit('object check needs the NativeLegacyDisplay.cpp context job in the capture')
        rec=ms[0];old=rec['file'].split('/research/',1)[0];project=base/'build/project'
        def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
        flags=[];i=0
        while i<len(rec['arguments']):
            v=rec['arguments'][i]
            if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
            if v==rec['file'] or v in ('-MD','-MP','-MMD'):i+=1;continue
            if v=='-Wno-unnecessary-virtual-specifier':i+=1;continue
            if v.startswith(('-DELDEN_','-DNATIVE_UFS_','-DUFS_ROOT_')):i+=1;continue
            flags.append(remap(v));i+=1
        flags+=['-Werror',*[x for d in (derived,gpu,kits/'ArmSmmu4K',kits/'AdrenoPM4') for x in ('-I',str(d))]]
        objcmd=[*flags,str(derived/'NativeAdrenoSubmit.cpp'),'-o',str(obj)]
        record['object_command']=list(map(str,objcmd))
        env=dict(os.environ,**(cap.get('environment') or {}))
        proc=subprocess.run(objcmd,cwd=remap(rec['directory']),env=env,capture_output=True,timeout=300)
        (out/'object.log').write_bytes(proc.stdout+proc.stderr);record['object_rc']=proc.returncode
        if record['object_rc']==0:record['object_sha256']=sha(obj)
    else:
        record['object_rc']='skipped';record['object_note']='no --base-native; build_gpu_native.py --submit is the authoritative native compile'
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    if not record['pass']:raise SystemExit('Test failed; see '+str(out/'run.log'))
    print('PASS: submit timing host suite' + ('; arm64 object clean' if record['object_rc']==0 else ''))
if __name__=='__main__':main()
