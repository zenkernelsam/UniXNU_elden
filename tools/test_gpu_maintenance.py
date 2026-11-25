"""Host-test the round-3 maintenance derivations; no device IO.

Compiles the derived maintenance/publish/vm/memory/gpuctx units (MaintenanceSource
over the pinned Adreno740-a830 sources, chained after RuntimeSource where it
applies) and the derived submit pair (SubmitSource) with the pinned tree's real
Ring/SmmuMemory/Preempt units over the software register peers, then runs
test_maintenance.cpp, test_gpuctx.cpp and test_submit.cpp under ASan/UBSan.
Host/model evidence only.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenGpu'
sys.path.insert(0,str(DRIVER))
from RuntimeSource import publisher,memory
from MaintenanceSource import derive as maint_derive,pin as maint_pin
from SubmitSource import derive as submit_derive

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd,log):
    proc=subprocess.run(list(map(str,cmd)),capture_output=True)
    Path(log).write_bytes(proc.stdout+proc.stderr)
    return proc.returncode

# test name -> (test TU, translation units resolved under (derived, gpu))
# NativeAdrenoPublish.cpp (the gated IOService publisher) uses real IOKit service
# APIs and is kernel-side only; the host suite exercises publishMapped inside the
# derived Vm unit like the timing suite does.
TESTS={
    'maintenance':('test_maintenance.cpp',
        ['derived/NativeAdrenoVm.cpp','derived/NativeAdrenoMemory.cpp',
         'gpu/NativeAdrenoSmmuMemory.cpp']),
    'gpuctx':('test_gpuctx.cpp',
        ['derived/NativeAdrenoGpuContext.cpp','derived/NativeAdrenoVm.cpp','derived/NativeAdrenoMemory.cpp',
         'gpu/NativeAdrenoSmmuMemory.cpp','gpu/NativeAdrenoSmmuContext.cpp']),
    'submit':('test_submit.cpp',
        ['derived/NativeAdrenoSubmit.cpp','gpu/NativeAdrenoRing.cpp','derived/NativeAdrenoVm.cpp',
         'derived/NativeAdrenoMemory.cpp','gpu/NativeAdrenoSmmuMemory.cpp','gpu/NativeAdrenoPreempt.cpp']),
}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('tree','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--derived',type=Path,help='Directory already holding the derived units; else they are derived here from --tree')
    p.add_argument('--llvm',type=Path,help='LLVM toolchain bin dir; default xcrun')
    p.add_argument('--test',action='append',choices=sorted(TESTS),help='Run only this test (repeatable); default all')
    a=p.parse_args()
    tree=a.tree.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    gpu=tree/'drivers/Adreno740'
    derived=a.derived.resolve() if a.derived else out/'derived'
    if not a.derived:
        derived.mkdir()
        mpin=maint_pin()
        for name in ('NativeAdrenoMemory.h','NativeAdrenoVm.h','NativeAdrenoVm.cpp','NativeAdrenoGpuContext.cpp'):
            src=(gpu/name).read_bytes()
            if hashlib.sha256(src).hexdigest()!=mpin['files'][name]:raise ValueError('Maintenance source differs from the derivation pin: '+name)
            (derived/name).write_bytes(maint_derive(name,src))
        for name,base in (('NativeAdrenoMemory.cpp',memory),('NativeAdrenoPublish.cpp',publisher)):
            src=(gpu/name).read_bytes()
            (derived/name).write_bytes(maint_derive(name,base(src)))
        spin=json.loads((DRIVER/'upstream/submit-source.json').read_text())
        sc=(gpu/'NativeAdrenoSubmit.cpp').read_bytes();sh=(gpu/'NativeAdrenoSubmit.h').read_bytes()
        for name,data in (('NativeAdrenoSubmit.cpp',sc),('NativeAdrenoSubmit.h',sh)):
            if hashlib.sha256(data).hexdigest()!=spin['files'][name]:raise ValueError('Submit source differs from the derivation pin: '+name)
        dcpp,dh=submit_derive(sc,sh)
        (derived/'NativeAdrenoSubmit.cpp').write_bytes(dcpp);(derived/'NativeAdrenoSubmit.h').write_bytes(dh)
    llvm=a.llvm
    cxx=[str(llvm/'clang++')] if llvm else ['xcrun','clang++']
    cc=[str(llvm/'clang')] if llvm else ['xcrun','clang']
    kits=tree/'kits'
    stubs=out/'host-headers'
    for rel in ('IOKit/IOTypes.h','IOKit/IOLib.h','IOKit/IOBufferMemoryDescriptor.h','kern/task.h',
                'IOKit/IOMemoryDescriptor.h','IOKit/IOService.h','IOKit/IOPlatformExpert.h',
                'pexpert/pexpert.h','IOKit/IOUserClient.h','kern/thread.h'):
        f=stubs/rel;f.parent.mkdir(parents=True,exist_ok=True)
        f.write_text('// Host only; upstream XnuMemoryFixture supplies doubles.\n')
    # The derived headers must shadow the tree headers for every TU (a tree header's
    # quoted include would otherwise resolve to the tree sibling and its identical
    # include guard would drop the derived declarations): the same -ivfsoverlay the
    # native build uses.
    contents=[{'type':'file','name':h.name,'external-contents':str(h)}
              for h in sorted(derived.glob('*.h')) if (gpu/h.name).is_file()]
    vfs=out/'derived-overlay.yaml'
    vfs.write_text(json.dumps({'version':0,'case-sensitive':False,'fallthrough':True,
        'roots':[{'type':'directory','name':str(gpu),'contents':contents}]},indent=2)+'\n')
    includes=['-ivfsoverlay',vfs,'-I',derived,'-I',gpu,'-I',gpu/'tests','-I',stubs,'-I',kits/'ArmSmmu4K','-I',kits/'AdrenoPM4',
              '-I',kits/'AdrenoFirmware','-I',kits/'Sm8750Platform','-I',DRIVER/'tests']
    flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
           '-DNATIVE_ADRENO_HOST_TEST','-include',gpu/'tests/XnuMemoryFixture.h',*includes]
    kits_c=sorted(x for x in (kits/'ArmSmmu4K').glob('ArmSmmu4K*.c') if x.name!='ArmSmmu4KMmio.c')+ \
        [kits/'AdrenoPM4'/'AdrenoPM4.c',kits/'AdrenoPM4'/'AdrenoConstants.c']
    kit_objs=[]
    for i,src in enumerate(kits_c):
        obj=out/('kit%d.o'%i)
        kc=[*cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I',kits/'ArmSmmu4K','-I',kits/'AdrenoPM4','-c',src,'-o',obj]
        if run(kc,out/('kit%d.log'%i)):raise SystemExit('Kit compile failed: '+src.name)
        kit_objs.append(obj)
    env=dict(os.environ);env['ASAN_OPTIONS']=(env.get('ASAN_OPTIONS','')+':detect_leaks=0').lstrip(':')
    record={'tests':{}}
    wanted=a.test or sorted(TESTS)
    for name in wanted:
        tu,units=TESTS[name]
        cmd=[*cxx,*flags,DRIVER/'tests'/tu,DRIVER/'tests'/'HostKernelHooks.cpp',
             *[(derived/u.split('/',1)[1]) if u.startswith('derived/') else (gpu/u.split('/',1)[1]) for u in units],
             *kit_objs,'-o',out/('test-'+name)]
        rc=run(cmd,out/(name+'-build.log'))
        entry={'command':list(map(str,cmd)),'build_rc':rc}
        if rc:raise SystemExit(name+' compile failed; see '+str(out/(name+'-build.log')))
        proc=subprocess.run([out/('test-'+name)],capture_output=True,env=env)
        (out/(name+'-run.log')).write_bytes(proc.stdout+proc.stderr)
        entry['run_rc']=proc.returncode
        entry['pass']='PASS' in (out/(name+'-run.log')).read_text(errors='replace') and proc.returncode==0
        record['tests'][name]=entry
        if not entry['pass']:raise SystemExit(name+' failed; see '+str(out/(name+'-run.log')))
    record['pass']=True
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print('PASS: maintenance host suite ('+', '.join(wanted)+')')
if __name__=='__main__':main()
