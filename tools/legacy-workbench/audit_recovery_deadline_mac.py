"""Inspect actual recovery functions and archive membership after native build."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
root=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==root
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);a=p.parse_args();assert a.version>=39
tree=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
directory=tree/'build/sheng-ufs';obj=directory/'ShengUfs.o';archive=directory/'libShengUfs.a'
out=root/f'out/startup-basic-bundle-v{a.version}-20261108/recovery-codegen';out.mkdir(exist_ok=True)
assert not (out/'audit.json').exists()
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
def run(args):return subprocess.run([str(x) for x in args],capture_output=True,check=True).stdout
sha=lambda x:hashlib.sha256(x).hexdigest()
data=obj.read_bytes();member=run([llvm/'llvm-ar','p',archive,obj.name])
assert member==data or member==data+b'\n'*((-len(data))%8)
symbols=['__ZL14eldenRpcWorkerPvi','__ZL19eldenDeadlineWorkerPvi','__ZL21eldenRecoveryShutdownv',
         '__ZL26eldenStartRecoveryDeadlinev','_elden_recovery_deadline_remaining']
records={}
for name in symbols:
    text=run([llvm/'llvm-objdump','--disassemble-symbols='+name,'--reloc',obj]).decode()
    instructions=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M)
    assert instructions,name
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',line) for line in instructions),name
    (out/(name+'.txt')).write_text(text)
    records[name]=dict(instructions=len(instructions),fp_simd_operands=False)
deadline=(out/'__ZL19eldenDeadlineWorkerPvi.txt').read_text()
assert '__ZL21eldenRecoveryShutdownv' in deadline
assert 'AckComplete' not in deadline
shutdown=(out/'__ZL21eldenRecoveryShutdownv.txt').read_text()
assert '_reboot_kernel' in shutdown and '_elden_touch_request_stop' in shutdown and '_elden_thermal_request_stop' in shutdown
if a.version>=46:assert '_elden_backlight_request_stop' in shutdown
undefined=run([llvm/'llvm-nm','-u',obj]).decode();assert '_early_random' in undefined
report=dict(accepted=True,object_sha256=sha(data),archive_sha256=sha(archive.read_bytes()),
            archive_member_equal=True,functions=records,early_random_import=True,
            sources={n:sha((directory/'src'/n).read_bytes()) for n in ['EldenSdamRecovery.inc','EldenRebootOwnership.h','EldenUsbDebugXnu.inc']},
            hardware_tested=False)
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
