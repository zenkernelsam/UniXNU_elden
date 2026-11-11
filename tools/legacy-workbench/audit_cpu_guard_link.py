"""Audit the actual guarded CPU8 prelink, not a stand-alone object."""
from pathlib import Path
import json,hashlib,struct,sys,zipfile,re
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];p=r/'research/cpu-guard-20261111';out=p/'linked';out.mkdir(exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest();archive=p/'audit-export.zip'
assert sha(archive.read_bytes())=='073df4120cb60751596b9a41379155cf33591553330d3e47f4f13db057f60c60'
with zipfile.ZipFile(archive) as z:
 manifest=json.loads(z.read('manifest.json'));assert set(z.namelist())==set(manifest)|{'manifest.json'}
 for n,h in manifest.items():
  b=z.read(n);assert sha(b)==h
  f=out/n;assert f.resolve().is_relative_to(out.resolve());f.write_bytes(b)
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
from darwinboot_preflight import parse_kernel
b=(out/'kernel.macho').read_bytes();parse_kernel(b);s=m.symbols(b);addresses=sorted(set(s.values()));rev={v:n for n,v in s.items()}
meta=json.loads((out/'kernel.json').read_text());result=json.loads((out/'result.json').read_text());native=json.loads((out/'native-audit.json').read_text())
assert sha(b)==result['kernel_sha256']==meta['candidate_sha256']
assert result['armcpu_object_sha256']==native['objects']['armcpu']['object_sha256']
for n,h in meta['original_executable_sha256'].items():
 e=next(e for e in meta['extents'] if e['module']==n and e['segment']=='__TEXT_EXEC');offset=m.locate(b,e['va'])[1];assert sha(b[offset:offset+e['size']])==h,n
assert len(meta['original_executable_sha256'])==30
layout=json.loads((out/'layout.json').read_text());assert layout['max_cpus']==8 and layout['max_clusters']==2 and layout['clutch_multiple_offset']==232
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')}
assert spans=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144}
assert struct.unpack_from('<I',b,m.locate(b,s['_sched_clutch_dispatch'])[1]+232)[0]==0
records={}
def inspect(name):
 a=s[name];end=next(v for v in addresses if v>a);offset=m.locate(b,a,executable=True)[1]
 ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[offset:offset+end-a],a));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),name
 calls=[rev.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 text='\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}'+(' ; '+rev.get(int(i.op_str[1:],16),'') if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x') else '') for i in ins)+'\n'
 (out/(name+'.asm')).write_text(text);records[name]={'address':hex(a),'instructions':len(ins),'fp_simd':False,'calls':calls};return text,calls
names=['_reset_vector','_ml_get_cpu_number','__ZN6ArmCpu12drainPendingEv','__ZN6ArmCpu18releaseSecondariesEv',
       '_elden_armcpu_guard_ipi','_elden_armcpu_guard_processor','_elden_smp_recovery_ready','_elden_panic_return_to_fastboot']
names += [n for n in s if 'eldenSmpGuardWorker' in n or 'eldenSmpWorkThread' in n or 'eldenSmpSmc' in n]
for name in names:inspect(name)
assert 'and x0, x15, #0xffffff' in (out/'_reset_vector.asm').read_text()
assert '#0xffffff' in (out/'_ml_get_cpu_number.asm').read_text()
calls=records['__ZN6ArmCpu12drainPendingEv']['calls'];assert '_elden_smp_recovery_ready' in calls and '__ZN6ArmCpu15finishSecondaryEv' in calls
assert calls.index('_elden_smp_recovery_ready')<calls.index('__ZN6ArmCpu15finishSecondaryEv')
ready=records['_elden_smp_recovery_ready']['calls'];assert not any(n in ready for n in ('_IOSleep','_thread_block','_kernel_thread_start','_elden_vdog_smc'))
worker=next(v for n,v in records.items() if 'eldenSmpGuardWorker' in n)
assert '_elden_armcpu_release_secondaries' in worker['calls'] and '_kernel_thread_start' in worker['calls']
work=next(v for n,v in records.items() if 'eldenSmpWorkThread' in n)
assert {'_elden_armcpu_guard_processor','_elden_armcpu_guard_ipi','_thread_bind','_thread_block'}<=set(work['calls'])
for n in ('ArmCpu.cpp','EldenSdamRecovery.inc'):assert sha((out/n).read_bytes())==result['source']['files'][n]
link=(out/'linkarchives.txt').read_text();assert '/cpu-full-20261111/' not in link and '/out/battery-telemetry-v35-' not in link
assert '/cpu-guard-20261111/tree/build/armcpu/libArmCpu.a' in link
report={'accepted_cpu_guard_link':True,'kernel_sha256':sha(b),'original_modules':30,'cpu_array_bytes':spans,'clutch_single_pset':True,
        'functions':records,'boot_image_qualified':False,'hardware_tested':False,'limits':['original ABI/assets and signed boot packaging still pending','guarded SMP,IPI isolation and panic keepalive remain untested on device']}
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='functions'},indent=2))
