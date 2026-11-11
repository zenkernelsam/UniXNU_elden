"""Freeze guarded-SMP source over the qualified CPU8 and watchdog inputs."""
from pathlib import Path
import hashlib,importlib.util,json,zipfile
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation')
p=r/'research/cpu-guard-20261111';p.mkdir(exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
qualified=json.loads((r/'research/cpu-activation-20261111/expiration-qualification-v80.json').read_text())
assert qualified['qualified'] and qualified['bark_ms']==27000 and qualified['bite_ms']==30000
for n,h in qualified['protocol_sources'].items():assert sha((d/n).read_bytes())==h,n
spec=importlib.util.spec_from_file_location('deferred',d/'Deferred.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
base=(r/'research/cpu-full-20261111/sources/ArmCpu.cpp').read_bytes();cpu=mod.apply(base)
before=(r/'research/basic-bundle-20261108/source-compiled-v80/usb/EldenSdamRecovery.inc').read_bytes();text=before.decode()
declaration='static void eldenStartVdogTrial(void);';assert text.count(declaration)==1
text=text.replace(declaration,declaration+'\nstatic void eldenStartSmpGuard(void);\nstatic void eldenSmpPanicPet(void);')
anchor='  if(mode!=1)return;';assert text.count(anchor)==1
text=text.replace(anchor,'  if(mode==2){eldenStartSmpGuard();return;}\n'+anchor)
anchor='do{elden_usb_panic_drain();__asm__';assert text.count(anchor)==1
text=text.replace(anchor,'do{eldenSmpPanicPet();elden_usb_panic_drain();__asm__')
body=(d/'GuardPolicy.h').read_text().replace('#pragma once\n','')+'\n'+(d/'SmpGuard.inc').read_text()
end=text.rfind('#endif');text=text[:end]+'\n/* ELDEN_SMP_GUARD_BEGIN */\n'+body+'\n/* ELDEN_SMP_GUARD_END */\n'+text[end:]
inputs={'ArmCpu.cpp':cpu,'EldenSdamRecovery.inc':text.encode(),'GuardPolicy.h':(d/'GuardPolicy.h').read_bytes(),'SmpGuard.inc':(d/'SmpGuard.inc').read_bytes(),'Deferred.py':(d/'Deferred.py').read_bytes()}
for n,b in inputs.items():(p/n).write_bytes(b)
# Queue/release algorithms retain their already-tested semantics; the added
# diagnostics are after finishSecondary and do not change these methods.
old=(r/'research/cpu-activation-20261111/ArmCpu.deferred.cpp').read_text()
new=cpu.decode();start='void ArmCpu::drainPending (void)';stop='bool ArmCpu::finishSecondary (void)'
assert old[old.index(start):old.index(stop)]==new[new.index(start):new.index(stop)]
report={'files':{n:sha(b) for n,b in inputs.items()},'cpu8_source_sha256':sha(base),'v80_recovery_sha256':sha(before),
        'watchdog_qualification_sha256':sha((r/'research/cpu-activation-20261111/expiration-qualification-v80.json').read_bytes()),
        'queue_release_methods_unchanged':True,'smp_hardware_tested':False,'boot_image_created':False}
(p/'source.json').write_text(json.dumps(report,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in inputs.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
