from pathlib import Path
import argparse,hashlib,json,re,sys,struct
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,choices=[83,84],default=83);version=parser.parse_args().version
r=Path(__file__).resolve().parents[1];p=r/f'edl_work/basic-bundle-20261108/v{version}';src=r/f'research/basic-bundle-20261108/source-compiled-v{version}';old=r/'research/basic-bundle-20261108/source-compiled-v81';sha=lambda b:hashlib.sha256(b).hexdigest()
build=json.loads((p/'final/cpu-irq-snapshot-result.json').read_text());der=json.loads((p/'final/derivative.json').read_text());b=(p/'final/kernel.macho').read_bytes()
assert build['parent_unchanged'] and build['control_runtime_sections_and_symbols_equal'] and build['source']['hardware_writes_added'] is False
assert der['source_sha256']==build['kernel_sha256'] and der['derivative_sha256']==sha(b)
if version==83:
 for n in ('ArmCpu.cpp','GuardPolicy.h','Deferred.py'):assert (src/'cpu'/n).read_bytes()==(old/'cpu'/n).read_bytes(),n
else:
 original=(old/'cpu/ArmCpu.cpp').read_text();needle='  return *reinterpret_cast<volatile uint32_t *>(base + offset);'
 replacement='  uint32_t value;const uintptr_t address=base+offset;\n  __asm__ volatile("ldr %w0,[%1]":"=r"(value):"r"(address):"memory");\n  return value;'
 assert original.count(needle)==1 and original.replace(needle,replacement)==(src/'cpu/ArmCpu.cpp').read_text()
 assert (src/'cpu/GuardPolicy.h').read_bytes()==(old/'cpu/GuardPolicy.h').read_bytes()
current=(src/'cpu/SmpGuard.inc').read_text();before=(old/'cpu/SmpGuard.inc').read_text()
start=current.index('\nstatic void eldenSmpReadGic(');end=current.index('\n}\n',start)+3
stripped=current[:start]+current[end:]
for text in ('extern "C" void armcpu_gic_diag(uint64_t,uint32_t *);\n','extern "C" volatile uint32_t armcpu_gic_boot[8],armcpu_gic_diag_stage;\n','      eldenSmpReadGic(0);\n','        eldenSmpReadGic(i);\n'):
 assert stripped.count(text)==1;stripped=stripped.replace(text,'')
assert stripped.replace('\n\n\n','\n\n')==before.replace('\n\n\n','\n\n')
assert (src/'usb/EldenSdamRecovery.inc').read_text().replace(current,before)==(old/'usb/EldenSdamRecovery.inc').read_text()
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
s=m.symbols(b);addresses=sorted(set(s.values()));reverse={v:n for n,v in s.items()};records={}
for name in s:
 if not ('eldenSmpReadGic' in name or 'eldenSmpGuardWorker' in name or name=='_armcpu_gic_diag'):continue
 a=s[name];end=next(v for v in addresses if v>a);off=m.locate(b,a,executable=True)[1];ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-a],a));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),name
 calls=[reverse.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 records[name]={'calls':calls,'instructions':len(ins),'fp_simd':False}
 if version==84 and name=='_armcpu_gic_diag':
  plain=[i for i in ins if i.mnemonic=='ldr' and re.fullmatch(r'w\d+, \[x\d+\]',i.op_str)]
  bad=[i for i in ins if i.mnemonic in ('ldr','ldp','ldur') and '[sp' not in i.op_str and (']!' in i.op_str or '],' in i.op_str or i.mnemonic=='ldur')]
  assert len(plain)>=7 and not bad
  records[name]['plain_mmio_loads']=len(plain)
 (p/(name+'.asm')).write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in ins)+'\n')
assert any('_armcpu_gic_diag' in v['calls'] for v in records.values())
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')}
assert spans=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144}
assert struct.unpack_from('<I',b,m.locate(b,s['_sched_clutch_dispatch'])[1]+232)[0]==0
result={'accepted':True,'kernel_sha256':sha(b),'prelink_sha256':build['kernel_sha256'],'source':build['source'],'source_link_reproduced':True,'functions':records,'array_bytes':spans,'hardware_writes_added':False,'hardware_tested':False}
(p/'cpu-irq-snapshot-audit.json').write_text(json.dumps(result,indent=2)+'\n');print({'accepted':True,'kernel_sha256':sha(b),'functions':len(records)})
