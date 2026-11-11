"""Compile the board battery publisher with the admitted iOS17 PMIC context.

Run only from the Mac Y700 repository. Preserves all prior archives/candidates;
the new class initializer is deferred until explicit, qualified activation.
"""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path

ROOT=Path.cwd().resolve()
assert ROOT==Path('/Users/ciscohe/Desktop/UniXNU_elden')
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--attempt',type=int,default=1)
args=parser.parse_args();assert 1<=args.attempt<=99
suffix='' if args.attempt==1 else '-v'+str(args.attempt)
OUT=ROOT/('out/battery-telemetry'+suffix+'-20261108');OUT.mkdir(exist_ok=True)
SOURCE=ROOT/('research/battery-telemetry-20261108/source-input'+suffix)
LLVM=Path('/Users/ciscohe/toolchains/llvm-20/bin')
sys.path.insert(0,str(ROOT/'third_party/UniXNU/kits/XnuPrelink'))
from native_prelink import macho

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(cmd,cwd=None):
    r=subprocess.run([str(x) for x in cmd],cwd=cwd,capture_output=True)
    if r.returncode:
        sys.stdout.write(r.stdout.decode(errors='replace'));sys.stderr.write(r.stderr.decode(errors='replace'))
        raise RuntimeError('Compile tool failed: '+str(cmd[0]))
    return r.stdout
def context(group):
    d=ROOT/'out/adsp-pmic-ucsi45-20261108'/group
    report=json.loads((d/'build.json').read_text())
    assert report['arm64_ios17'] and not report['automatic_initializers']
    assert sha(Path(report['archive']))==report['archive_sha256']
    for name,digest in report['objects'].items():assert sha(d/name)==digest,name
    return d,report
def command(old,file,out,extra=()):
    cmd=list(old);inputs=[i for i,x in enumerate(cmd) if x.endswith(('.cpp','.c')) and not x.startswith('-')]
    assert len(inputs)==1,inputs
    cmd[inputs[0]]=str(SOURCE/file);cmd[cmd.index('-o')+1]=str(out)
    cmd+=['-I',str(SOURCE),*extra]
    return cmd
def text_code(path):
    b=path.read_bytes();m=macho(b)
    s=next(s for g in m['segments'] for s in g['sections'] if s['name']=='__text')
    return b[s['fileoff']:s['fileoff']+s['size']]
def admit(group,kind,objects,commands,reused=None):
    d=OUT/group;archive=d/('lib'+group+'.a')
    run([LLVM/'llvm-ar','rcsD',archive,*[d/n for n in objects]])
    for n in objects:
        m=macho((d/n).read_bytes())
        assert not any(s['name']=='__mod_init_func' and s['size'] for g in m['segments'] for s in g['sections']),n
    report=dict(name=group,kind=kind,archive=str(archive),archive_sha256=sha(archive),
        archive_bytes=archive.stat().st_size,objects={n:sha(d/n) for n in objects},commands=commands,
        reused=reused or {},arm64_ios17=True,automatic_initializers=False,hardware_enabled=False,
        source_files={p.name:sha(p) for p in SOURCE.iterdir() if p.is_file()})
    (d/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

adsp_old,adsp=context('EldenAdspRuntime')
pmic_old,pmic=context('EldenPmicClient')
work=ROOT/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
cwd=json.loads((work/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))['directory']
reports=[]
d=OUT/'EldenAdspRuntime';d.mkdir(exist_ok=True);objects=list(adsp['objects']);cmds=[]
for name in objects:
    if name in ('AdspProbe.o','AdspFirmware.o'):
        file='AdspProbe.cpp' if name=='AdspProbe.o' else 'AdspFirmware.c'
        old=next(c for c in adsp['commands'] if any(x.endswith('/'+file) for x in c))
        cmd=command(old,file,d/name);run(cmd,cwd);cmds.append(cmd)
    else:shutil.copyfile(adsp_old/name,d/name)
reports.append(admit('EldenAdspRuntime','original M3 runtime with shared AOSS owner guard',objects,cmds,
    {n:adsp['objects'][n] for n in objects if n not in ('AdspProbe.o','AdspFirmware.o')}))
d=OUT/'EldenPmicClient';d.mkdir(exist_ok=True);objects=[];cmds=[]
cpp=next(c for c in pmic['commands'] if any(x.endswith('/PmicClient.cpp') for x in c))
cc=next(c for c in pmic['commands'] if any(x.endswith('/ChargePoll.c') for x in c))
for name in ['PmicClient.cpp','CpuPerfSurvey.cpp','ChargePoll.c','ChargeRead.c','ChargeNotify.c','UsbIcl.c','ChargeVotes.c','ChargePolicy.c','UcsiQuery.c','BatterySnapshot.c','BatteryRuntime.cpp',
             'Adc.c','Spmi.c','ThermScale.c','Policy.c','UsbThermalRuntime.cpp','Pins.c','Control.c','ProtectionRuntime.cpp','PdContract.c','PdBootstrap.c','PowerPathRead.c','UlogRead.c','DcNoop.c','UlogFilter.c','DcSpmiSurvey.cpp','DcTrial.c','DcNative.cpp']:
    target=d/(Path(name).stem+'.o')
    cmd=command(cpp if name.endswith('.cpp') else cc,name,target,['-DELDEN_BATTERY_RUNTIME=1'])
    run(cmd,cwd);cmds.append(cmd);objects.append(target.name)
raw=d/'BatteryRuntime.o';late=d/'BatteryRuntime.late.o'
run([LLVM/'llvm-objcopy','--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_BatteryRuntime.cpp',raw,late])
assert text_code(raw)==text_code(late),'Deferred class registration must not alter instructions'
objects[objects.index(raw.name)]=late.name
names=run([LLVM/'llvm-nm','--defined-only',late]).decode()
assert '__GLOBAL__sub_I_BatteryRuntime.cpp' in names and '__ZTV12EldenBattery' in names
reports.append(admit('EldenPmicClient','Lenovo telemetry, admitted USB/charge controls and gated IOPMPowerSource publisher',objects,cmds))
result=dict(accepted=True,device_operations=False,profiles_default_off=True,
    class_initializer_deferred=True,shared_adsp_owner_updated=True,
    archives=[dict(name=r['name'],sha256=r['archive_sha256'],bytes=r['archive_bytes']) for r in reports])
(OUT/'compile-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
