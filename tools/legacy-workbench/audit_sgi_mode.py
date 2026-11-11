from pathlib import Path
import hashlib,json,re,struct,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];p=r/'edl_work/basic-bundle-20261108/v85';src=r/'research/basic-bundle-20261108/source-compiled-v85';sha=lambda b:hashlib.sha256(b).hexdigest()
b=(p/'final/kernel.macho').read_bytes();build=json.loads((p/'final/cpu-irq-snapshot-result.json').read_text());der=json.loads((p/'final/derivative.json').read_text())
assert build['parent_unchanged'] and build['control_runtime_sections_and_symbols_equal'] and build['source']['capability_guarded']
assert der['source_sha256']==build['kernel_sha256'] and der['derivative_sha256']==sha(b)
for n in ('ArmCpu.cpp','SmpGuard.inc','GuardPolicy.h','Deferred.py','SgiMode.h'):assert sha((src/'cpu'/n).read_bytes())==build['source']['files'][n]
host=json.loads((r/'research/cpu-ipi-20261111/sgi-mode-test.json').read_text());assert host['passed'] and host['source_sha256']==sha((src/'cpu/SgiMode.h').read_bytes())
text=(src/'cpu/SgiMode.h').read_text();assert 'if(!(r->capability&0x100U))' in text and 'r->before&0x53U' in text
assert text.index('write(off);')<text.index('write(off|0x100U)')<text.index('write((r->before&~0x100U)')
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
s=m.symbols(b);addresses=sorted(set(s.values()));reverse={v:n for n,v in s.items()};records={}
for n in s:
 if not ('guardSgiMode' in n or n=='_elden_armcpu_sgi_mode' or 'eldenSmpGuardWorker' in n or n=='_armcpu_gic_diag'):continue
 start=s[n];end=next(a for a in addresses if a>start);off=m.locate(b,start,executable=True)[1]
 ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-start],start));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),n
 calls=[reverse.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 records[n]={'instructions':len(ins),'calls':calls,'fp_simd':False}
 (p/(n+'.asm')).write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in ins)+'\n')
 if n=='_armcpu_gic_diag':
  assert len([i for i in ins if i.mnemonic=='ldr' and re.fullmatch(r'w\d+, \[x\d+\]',i.op_str)])>=7
  assert not any(i.mnemonic in ('ldr','ldp','ldur') and '[sp' not in i.op_str and (']!' in i.op_str or '],' in i.op_str or i.mnemonic=='ldur') for i in ins)
worker=next(v for n,v in records.items() if 'eldenSmpGuardWorker' in n)['calls']
assert worker.index('_elden_vdog_arm')<worker.index('_elden_armcpu_sgi_mode')<worker.index('_elden_armcpu_release_secondaries')
mode=next(v for n,v in records.items() if 'guardSgiMode' in n)['calls'];assert mode.count('_ml_set_interrupts_enabled')==2
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')};assert spans=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144}
assert struct.unpack_from('<I',b,m.locate(b,s['_sched_clutch_dispatch'])[1]+232)[0]==0
out={'accepted':True,'kernel_sha256':sha(b),'prelink_sha256':build['kernel_sha256'],'source':build['source'],'source_link_reproduced':True,'functions':records,'array_bytes':spans,'capability_guarded':True,'hardware_tested':False}
(p/'sgi-mode-audit.json').write_text(json.dumps(out,indent=2)+'\n');print({'accepted':True,'kernel_sha256':sha(b),'functions':len(records)})
