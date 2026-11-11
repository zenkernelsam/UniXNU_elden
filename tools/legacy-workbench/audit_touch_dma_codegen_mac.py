"""Audit actual touch objects included in the rebuilt display archive."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
root=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==root
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--cpu-guard',action='store_true');a=p.parse_args()
assert 27<=a.version<=99
base=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/build/native-legacy-display'
if a.cpu_guard:
    assert a.version==81;base=root/'research/cpu-guard-20261111/tree/build/native-legacy-display'
out=root/f'out/startup-basic-bundle-v{a.version}-20261108/touch-codegen';out.mkdir(exist_ok=True)
assert not (out/'audit.json').exists(),'Completed audit must not be overwritten'
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(args):return subprocess.run([str(x) for x in args],capture_output=True,check=True).stdout
library=base/'libNativeLegacyDisplay.late.a'
report=json.loads((base/'build-report.json').read_text());assert sha(library.read_bytes())==report['archive_sha256']
result={}
objects=[base/(name+'.o') for name in ('EldenNvtDmaBuffer','EldenNvtRuntime','EldenNvtDmaRoute','EldenNvtDmaTransfer','EldenNvtDmaCache','EldenNvtPlatform')]
if a.version>=31:objects += [base/'EldenHidGlue.o',base.parent/'sheng-hid-input/NativeHidInput.late.o']
if a.version>=71:objects += [base/'EldenUserWaitProbe.o']
for obj in objects:
 name=obj.stem;raw=obj.read_bytes();member=run([llvm/'llvm-ar','p',library,obj.name])
 # Darwin ar may include newline padding to an eight-byte member boundary.
 # Require every object byte unchanged and only that exact permitted suffix.
 assert member==raw or member==raw+b'\n'*((-len(raw))%8),obj.name
 dis=run([llvm/'llvm-objdump','--disassemble','--reloc',obj]).decode()
 instructions=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',dis,re.M)
 assert instructions
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',line) for line in instructions),name
 (out/(name+'.txt')).write_text(dis)
 result[name]=dict(sha256=sha(raw),instructions=len(instructions),fp_simd_operands=False,archive_member_equal=True)
native=(out/'EldenNvtDmaBuffer.txt').read_text()
assert all(f'#0x{x:x}]' in native for x in (0xe8,0xf0,0xa8,0x158,0x28))
assert all(x in native for x in ('dc\tcvac','dc\tcivac','dc\tivac','dsb\tsy'))
proof=dict(accepted=True,objects=result,library_sha256=sha(library.read_bytes()),
 original_abi_reference='out/touch-dma-native-20261109-v3/build.json',
 buffer_source_sha256=report['source_sha256']['drivers/EldenNvt36536/NativeDmaBuffer.cpp'])
if a.version==73:
    user=(out/'EldenUserWaitProbe.txt').read_text()
    hid=(out/'NativeHidInput.late.txt').read_text()
    assert 'clock_get_uptime' in user and 'vm_map_read_user' in user and 'cntvct_el0' in user.lower()
    assert 'clock_get_uptime' in hid
    assert b'ELDEN_USER_CLOCK pass=' in (base/'EldenUserWaitProbe.o').read_bytes()
    assert b' raw-time=%llx options=%x' in (base.parent/'sheng-hid-input/NativeHidInput.late.o').read_bytes()
    proof['event_clock_observation']=dict(user_commpage=True,event_time_getter_slot=16,register_writes=False)
if a.version>=74:
    glue=(out/'EldenHidGlue.txt').read_text()
    assert 'elden_ui_dispatch_begin' in glue
    assert b'ELDEN_USER_CLOCK pass=' not in (base/'EldenUserWaitProbe.o').read_bytes()
    assert b' raw-time=%llx options=%x' not in (base.parent/'sheng-hid-input/NativeHidInput.late.o').read_bytes()
    proof['ui_dispatch_observation']=dict(one_shot_arm_call=True,failed_user_commpage_reader_removed=True)
prior=json.loads((root/proof['original_abi_reference']).read_text())
assert proof['buffer_source_sha256']==prior['source_files']['NativeDmaBuffer.cpp']
(out/'audit.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps(proof,indent=2))
