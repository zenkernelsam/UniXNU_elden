"""Audit exact source delta, compiled objects and fault-path reachability in v89."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[89,90,91,92],default=89);v=a.parse_args().version
r=Path(__file__).resolve().parents[1];p=r/f'edl_work/basic-bundle-20261108/v{v}';src=r/f'research/basic-bundle-20261108/source-compiled-v{v}';old=r/'research/basic-bundle-20261108/source-compiled-v88'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();b=(p/'final/kernel.macho').read_bytes()
delta=json.loads((p/'final/fault-delta.json').read_text());host=json.loads((p/'final/fault-host.json').read_text());build=json.loads((p/'final/cpu-irq-snapshot-result.json').read_text())
assert delta['host_passed'] and host['passed'] and not delta['fp_simd'] and build['fault_control_reproduced']
assert sha(p/'final/cpu-archives/libShengUfs.a')==delta['archive_sha256']
assert sha(p/'final/cpu-archives/ShengUfs.o')==delta['object_sha256']
assert sha(p/'final/cpu-archives/IconFaultCapture.o')==delta['capture_object_sha256']
for n,h in delta['source']['files'].items():assert sha(src/'usb'/n)==h
assert host['policy_sha256']==sha(src/'usb/FaultFrames.h') and host['test_sha256']==sha(src/'usb/TestFaultFrames.c')
baseline=(old/'usb/EldenUsbDebugXnu.inc').read_text();new=(src/'usb/EldenUsbDebugXnu.inc').read_text()
assert new.replace('#include "IconFaultTrace.inc"\n','').replace('  eldenIconFaultTrace(pid,name,pc,lr);\n','')==baseline
capture=(src/'usb/IconFaultCapture.c').read_text();hook=(src/'usb/IconFaultTrace.inc').read_text()
assert 'get_saved_state_pc(state)!=pc||get_saved_state_lr(state)!=lr' in capture and 'copyin((user_addr_t)address,pair,16)' in capture
assert 'strcmp(name,"iconservicesagent")||pc!=0||!lr' in hook and '>=4' in hook
assert 'elden_usb_preview_line(line,(unsigned)n)' in hook and '16' in (src/'usb/FaultFrames.h').read_text()
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
syms=m.symbols(b);starts=sorted(set(syms.values()));reverse={a:n for n,a in syms.items()};records={}
names=['_elden_usb_user_fault','_elden_icon_fault_capture']+[n for n in syms if n.startswith('__ZL') and 'eldenIconFaultTrace' in n]
assert len(names)==3,names
names.append('_elden_fault_frames')
for n in names:
 a=syms[n];end=next(x for x in starts if x>a);off=m.locate(b,a)[1];data=b[off:off+end-a]
 ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(data,a));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),n
 calls=[reverse.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 records[n]={'address':hex(a),'instructions':len(ins),'sha256':hashlib.sha256(data).hexdigest(),'calls':sorted(set(calls)-{''}),'fp_simd':False}
 (p/(n+'.asm')).write_text('\n'.join(f'{i.address:x} {i.mnemonic} {i.op_str}' for i in ins)+'\n')
assert names[2] in records['_elden_usb_user_fault']['calls']
assert '_elden_icon_fault_capture' in records[names[2]]['calls']
assert '_elden_usb_preview_line' in records[names[2]]['calls']
assert '_elden_fault_frames' in records['_elden_icon_fault_capture']['calls']
assert '_copyin' in records['_elden_fault_frames']['calls']
out={'accepted':True,'kernel_sha256':hashlib.sha256(b).hexdigest(),'source':delta['source'],'functions':records,'max_fault_attempts':4,'max_frames':16,'user_memory_writes':False,'host_proof_sha256':sha(p/'final/fault-host.json'),'archive_sha256':delta['archive_sha256'],'hardware_tested':False}
(p/'fault-codegen.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'accepted':True,'functions':{k:v['instructions'] for k,v in records.items()}},indent=2))
