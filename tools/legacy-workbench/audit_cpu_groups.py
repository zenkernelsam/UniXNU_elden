"""Check the actual linked late GIC group fix and its immutable v81 parent."""
from pathlib import Path
import hashlib,json,struct,sys,re
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];p=r/'edl_work/basic-bundle-20261108/v82';src=r/'research/basic-bundle-20261108/source-compiled-v82';old=r/'research/basic-bundle-20261108/source-compiled-v81'
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
b=(p/'final/kernel.macho').read_bytes();s=m.symbols(b);addresses=sorted(set(s.values()));rev={v:n for n,v in s.items()};sha=lambda b:hashlib.sha256(b).hexdigest()
build=json.loads((p/'final/cpu-groups-result.json').read_text());der=json.loads((p/'final/derivative.json').read_text())
assert build['control_runtime_sections_and_symbols_equal'] and build['parent_unchanged']
assert build['kernel_sha256']==der['source_sha256'] and der['derivative_sha256']==sha(b)
for name in ('ArmCpu.cpp','SmpGuard.inc','GuardPolicy.h','Deferred.py','GicGroups.h'):assert sha((src/'cpu'/name).read_bytes())==build['source']['files'][name]
assert sha((src/'usb/EldenSdamRecovery.inc').read_bytes())==build['source']['files']['EldenSdamRecovery.inc']
for folder in ('power','backlight','xnu'):
 for f in (old/folder).rglob('*'):
  if f.is_file():assert (src/f.relative_to(old)).read_bytes()==f.read_bytes(),str(f)
for group,name in [('armcpu','ArmCpu'),('ufs','ShengUfs')]:
 assert sha((p/'final/cpu-archives'/(name+'.o')).read_bytes())==build['objects'][group]['object_sha256']
 assert sha((p/'final/cpu-archives'/('lib'+name+'.a')).read_bytes())==build['objects'][group]['archive_sha256']
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')}
assert spans=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144}
assert struct.unpack_from('<I',b,m.locate(b,s['_sched_clutch_dispatch'])[1]+232)[0]==0
records={}
for name in s:
 if not ('guardGroups' in name or name=='_elden_armcpu_guard_groups' or 'eldenSmpGuardWorker' in name or name=='_elden_smp_recovery_ready'):continue
 a=s[name];end=next(v for v in addresses if v>a);off=m.locate(b,a,executable=True)[1];ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-a],a));assert ins
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins),name
 calls=[rev.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
 text='\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in ins)+'\n';(p/(name+'.asm')).write_text(text)
 records[name]={'instructions':len(ins),'calls':calls,'fp_simd':False}
calls=next(v['calls'] for n,v in records.items() if 'eldenSmpGuardWorker' in n)
assert calls.index('_elden_vdog_arm')<calls.index('_elden_armcpu_guard_groups')<calls.index('_elden_armcpu_release_secondaries')
assert '__ZN6ArmCpu11guardGroupsEv' in records['_elden_armcpu_guard_groups']['calls']
group=(p/'__ZN6ArmCpu11guardGroupsEv.asm').read_text()
assert '#1' in group and '#0x12' in group
host=json.loads((r/'research/cpu-ipi-20261111/groups-test.json').read_text());assert host['passed'] and host['header_sha256']==sha((src/'cpu/GicGroups.h').read_bytes())
assert b'ELDEN_GIC_GROUPS rc=' in b
report={'accepted':True,'kernel_sha256':sha(b),'prelink_sha256':build['kernel_sha256'],'source':build['source'],'source_link_reproduced':True,'functions':records,'array_bytes':spans,'host_sha256':sha((r/'research/cpu-ipi-20261111/groups-test.json').read_bytes()),'hardware_tested':False}
(p/'cpu-groups-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'accepted':True,'kernel_sha256':sha(b),'functions':len(records)},indent=2))
