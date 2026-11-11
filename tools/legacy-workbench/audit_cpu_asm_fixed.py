from pathlib import Path
import argparse,difflib,hashlib,json,re,struct,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,choices=[86,87,88,89,90,91,92],default=86);version=parser.parse_args().version
r=Path(__file__).resolve().parents[1];p=r/f'edl_work/basic-bundle-20261108/v{version}';src=r/f'research/basic-bundle-20261108/source-compiled-v{version}';old=r/'research/basic-bundle-20261108/source-compiled-v84';sha=lambda b:hashlib.sha256(b).hexdigest()
build=json.loads((p/'final/cpu-irq-snapshot-result.json').read_text());der=json.loads((p/'final/derivative.json').read_text());b=(p/'final/kernel.macho').read_bytes()
assert build['parent_unchanged'] and build['control_runtime_sections_and_symbols_equal'] and build['source']['assembly_delimiters_fixed']
assert der['source_sha256']==build['kernel_sha256'] and der['derivative_sha256']==sha(b)
assert (src/'cpu/GuardPolicy.h').read_bytes()==(old/'cpu/GuardPolicy.h').read_bytes()
if version==86:
 assert (src/'cpu/SmpGuard.inc').read_bytes()==(old/'cpu/SmpGuard.inc').read_bytes()
 assert (src/'usb/EldenSdamRecovery.inc').read_bytes()==(old/'usb/EldenSdamRecovery.inc').read_bytes()
else:
 baseline=r/'research/basic-bundle-20261108/source-compiled-v86'
 assert (src/'cpu/ArmCpu.cpp').read_bytes()==(baseline/'cpu/ArmCpu.cpp').read_bytes()
 bg=(baseline/'cpu/SmpGuard.inc').read_text();ng=(src/'cpu/SmpGuard.inc').read_text()
 assert all(x[0] in ('equal','insert') for x in difflib.SequenceMatcher(None,bg.splitlines(),ng.splitlines(),autojunk=False).get_opcodes())
 assert (src/'usb/EldenSdamRecovery.inc').read_text()==(baseline/'usb/EldenSdamRecovery.inc').read_text().replace(bg,ng.replace('#include "HoldPolicy.h"',(src/'cpu/HoldPolicy.h').read_text()))
 hold=json.loads((r/'research/cpu-hold-20261111/host-policy.json').read_text())
 assert hold['passed'] and hold['policy_sha256']==sha((src/'cpu/HoldPolicy.h').read_bytes()) and hold['test_sha256']==sha((src/'cpu/TestHoldPolicy.c').read_bytes())
text=(old/'cpu/ArmCpu.cpp').read_text().replace('msr icc_sgi1r_el1, %0; isb; mrs %1, cntvct_el0','msr icc_sgi1r_el1, %1; isb; mrs %0, cntvct_el0')
pattern=re.compile(r'(__asm__\s+volatile\s*\(\s*")((?:[^"\\]|\\.)*)(")')
fixed=pattern.sub(lambda m:m[1]+m[2].replace(';',r'\n\t')+m[3],text)
assert fixed==(src/'cpu/ArmCpu.cpp').read_text()
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
s=m.symbols(b);addresses=sorted(set(s.values()));records={};decoded={}
names=['__ZN6ArmCpu9signalCPUEP5IOCPU','__ZN6ArmCpu9enableGicEv','__ZN6ArmCpu17enableCPUTimeBaseEb','__ZN6ArmCpu9interruptEv','__ZN6ArmCpu17wakeRedistributorEb','_armcpu_gic_diag']
if version in (87,88,89,90,91,92):
 names += [n for n in s if 'eldenSmpHoldWindow' in n or 'eldenSmpGuardWorker' in n]
 assert len(names)>=8
for name in names:
 a=s[name];end=next(v for v in addresses if v>a);off=m.locate(b,a,executable=True)[1];ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-a],a));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),name
 decoded[name]=ins;records[name]={'instructions':len(ins),'fp_simd':False}
 (p/(name+'.asm')).write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in ins)+'\n')
sig=decoded[names[0]];index=[i for i,x in enumerate(sig) if x.mnemonic=='msr' and x.op_str.startswith('icc_sgi1r_el1,')]
assert len(index)==1 and sig[index[0]-1].mnemonic=='dsb' and sig[index[0]+1].mnemonic=='isb'
for name,reg in [(names[1],'icc_sre_el1'),(names[2],'cntv_ctl_el0'),(names[3],'icc_eoir1_el1')]:
 ins=decoded[name];i=next(i for i,x in enumerate(ins) if x.mnemonic=='msr' and x.op_str.startswith(reg+','));assert ins[i+1].mnemonic=='isb'
assert any(i.mnemonic=='mrs' and 'cntvct_el0' in i.op_str for i in decoded[names[4]])
assert len([i for i in decoded['_armcpu_gic_diag'] if i.mnemonic=='ldr' and re.fullmatch(r'w\d+, \[x\d+\]',i.op_str)])>=7
before=(r/'edl_work/basic-bundle-20261108/v84/final/kernel.macho').read_bytes();bs=m.symbols(before);a=bs[names[0]];end=min(v for v in bs.values() if v>a);off=m.locate(before,a)[1]
assert not any(i.mnemonic=='msr' and 'icc_sgi1r_el1' in i.op_str for i in Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(before[off:off+end-a],a))
assert '_elden_armcpu_sgi_mode' not in s and b'ELDEN_SGI_MODE' not in b
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')};assert spans=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144}
assert struct.unpack_from('<I',b,m.locate(b,s['_sched_clutch_dispatch'])[1]+232)[0]==0
out={'accepted':True,'kernel_sha256':sha(b),'prelink_sha256':build['kernel_sha256'],'source':build['source'],'source_link_reproduced':True,'functions':records,'array_bytes':spans,'actual_sgi_msr_present':True,'old_sgi_msr_missing':True,'mode_control_removed':True,'hardware_tested':False}
if version in (87,88,89,90,91,92):
 assert b'ELDEN_SMP HOLD_BEGIN' in b and b'ELDEN_SMP HOLD_END' in b and b'smp-hold\0' in b
 out['hold_policy_sha256']=sha((src/'cpu/HoldPolicy.h').read_bytes());out['host_hold_sha256']=sha((r/'research/cpu-hold-20261111/host-policy.json').read_bytes())
 out['v86_cpu_source_preserved']=True;out['hold_seconds_max']=600
(p/'cpu-asm-audit.json').write_text(json.dumps(out,indent=2)+'\n');print({'accepted':True,'actual_sgi_msr_present':True,'kernel_sha256':sha(b)})
