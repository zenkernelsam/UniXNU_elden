"""Audit actual linked UIKit observation plus its frozen source delta."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts')
import make_entry_probe_kernel as maker
p=argparse.ArgumentParser();p.add_argument('--version',type=int,default=74);v=p.parse_args().version
out=r/f'edl_work/basic-bundle-20261108/v{v}';src=r/f'research/basic-bundle-20261108/source-compiled-v{v}'
old=r/'research/basic-bundle-20261108/source-compiled-v72';proof=r/'research/ui-dispatch-20261111'
b=(out/'final/kernel.macho').read_bytes();symbols=maker.symbols(b);starts=sorted(set(symbols.values()));rev={a:n for n,a in symbols.items()}
records={}
names=[n for n in symbols if n.startswith('_elden_ui_dispatch_') or 'settingsTapWorker' in n]
assert '_elden_ui_dispatch_begin' in names and '_elden_ui_dispatch_stop' in names,names
names+=['_kdebug_trace','_kdebug_trace64','_kdebug_commpage_state']
for name in names:
 a=symbols[name];end=next(x for x in starts if x>a);offset=maker.locate(b,a)[1];raw=b[offset:offset+end-a]
 instructions=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(raw,a));assert instructions
 fp=[x for x in instructions if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x.op_str)]
 assert not fp,(name,[(x.mnemonic,x.op_str) for x in fp])
 calls=[rev.get(int(i.op_str[1:],16),'') for i in instructions if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 records[name]=dict(address=hex(a),instructions=len(instructions),sha256=hashlib.sha256(raw).hexdigest(),fp_simd=bool(fp),vector_instructions=len(fp),calls=sorted(set(calls)-{''}))
 (out/(name+'.asm')).write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in instructions)+'\n')
assert any('_elden_ui_dispatch_begin' in records[n]['calls'] for n in names if 'settingsTapWorker' in n)
assert '_elden_icon_cpu_user_marker' in records['_kdebug_trace']['calls']
if '_elden_ui_dispatch_note' in records['_kdebug_trace']['calls']:
    assert {'_proc_uniqueid','_thread_tid','_mach_absolute_time'}<=set(records['_elden_ui_dispatch_note']['calls'])
else:
    assert {'_proc_uniqueid','_thread_tid','_mach_absolute_time'}<=set(records['_kdebug_trace']['calls'])
# If the compiler inlines the note into syscall180, retain and inspect its actual
# code and fixed format string; don't mistake a helper definition for reachability.
if '_elden_ui_dispatch_note' not in records['_kdebug_trace64']['calls']:
    # Clang inlines the complete note into syscall180. These imports plus the
    # actual event/type masks distinguish it from the pre-existing icon hook.
    required={'_proc_best_name','_proc_uniqueid','_thread_tid','_proc_pid','_mach_absolute_time','_lck_spin_lock_grp'}
    assert required<=set(records['_kdebug_trace64']['calls'])
    assembly=(out/'_kdebug_trace64.asm').read_text()
    assert '#0x1ac' in assembly and '#0x2b87' in assembly and '#0xfffffffc' in assembly and '#0xff' in assembly
    records['_kdebug_trace64']['ui_note_inlined']=True
assert '_elden_icon_cpu_user_marker' in records['_kdebug_trace64']['calls']
assert any('thread_call_enter_delayed' in x for x in records['_elden_ui_dispatch_begin']['calls'])
assert '_commpage_update_kdebug_state' in records['_elden_ui_dispatch_stop']['calls']
source=(src/'xnu/kdebug.c').read_text();expected=(proof/'kdebug-dispatch.c').read_text()
assert source==expected,'Exact generated kdebug patch expected'
for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h'):
 assert (src/'xnu'/name).read_bytes()==(Path('D:/Desktop/Git/UniXNU_elden/patches/xnu')/name).read_bytes()
 host=(proof/name).read_text();native=(src/'xnu'/name).read_text()
 if name.endswith('.inc'):native=native.replace('#include <kern/thread_call.h>','').replace('#include <pexpert/pexpert.h>','')
 assert host==native,'Host-tested control body must match the compiled native source'
assert b'ELDEN_USER_CLOCK pass=' not in b
files={}
glue='tree/scripts/driver-patches/EldenHidGlue.cpp'
new=(src/glue).read_text();before=(old/glue).read_text()
extra='extern "C" int elden_ui_dispatch_begin(void) __attribute__((weak_import));\n'
arm='''    int observation=elden_ui_dispatch_begin?elden_ui_dispatch_begin():0;
    char armLine[100];snprintf(armLine,sizeof(armLine),"\\nELDEN_UI_DISPATCH arm=%d window-seconds=30\\n",observation);say(armLine);
'''
assert new.count(extra)==1 and new.count(arm)==1
assert new.replace(extra,'').replace(arm,'')==before
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files[glue]=dict(old=sha(old/glue),new=sha(src/glue))
for n in ('tree/scripts/driver-patches/EldenUserWaitProbe.c','tree/build/sheng-hid-input/src/NativeHidInput.cpp','usb/EldenSdamRecovery.inc'):
 if v in (81,82,83,84,85,86,87,88,89,90,91,92) and n=='usb/EldenSdamRecovery.inc':
  from cpu_guard_qualification import verify as verify_cpu_guard
  verify_cpu_guard(r,v,b)
  continue
 if v in (78,79,80) and n=='usb/EldenSdamRecovery.inc':
  from vdog_qualification import verify as verify_vdog
  verify_vdog(r,v,b) # Independently checked exact additive recovery delta.
  continue
 assert (src/n).read_bytes()==(old/n).read_bytes(),n
for folder in ('power','backlight'):
 for f in (old/folder).iterdir():
  if f.is_file():assert (src/folder/f.name).read_bytes()==f.read_bytes(),f
report=dict(accepted=True,compiled_snapshot=True,controls_unchanged=True,files=files,
 kernel_sha256=hashlib.sha256(b).hexdigest(),functions=records,window_seconds=30,
 host_test_sha256=sha(proof/'host-test.log'),hardware_tested=False,
 host_sources={n:sha(proof/n) for n in ('test_dispatch.c','typefilter-host.inc','commpage-state-host.inc','EldenUiDispatch.inc','EldenUiDispatchCore.h')},
 kdebug_sha256=sha(src/'xnu/kdebug.c'),kdebug_before_sha256=sha(old/'xnu/kdebug.c'),
 ui_sources={n:sha(src/'xnu'/n) for n in ('EldenUiDispatch.inc','EldenUiDispatchCore.h','elden-ui-dispatch.patch')})
if v in (78,79,80):
 report['recovery_delta']=verify_vdog(r,v,b)
 report['controls_scope']='UI, HID and power unchanged; recovery delta separately qualified'
if v in (81,82,83,84,85,86,87,88,89,90,91,92):
 report['cpu_guard_delta']=verify_cpu_guard(r,v,b)
 report['controls_scope']='UI,HID,power sources unchanged; guarded CPU8/recovery separately audited'
(proof/f'v{v}-source-delta.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'ui-dispatch-codegen.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'accepted':True,'functions':list(records),'kernel_sha256':report['kernel_sha256'],'controls_unchanged':True},indent=2))
