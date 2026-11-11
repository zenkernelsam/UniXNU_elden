"""Audit actual CPU8 prelink, retaining a clear boundary before boot qualification."""
from pathlib import Path
import hashlib,json,re,struct,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts')
import make_entry_probe_kernel as m
from darwinboot_preflight import parse_kernel
p=ROOT/'research/cpu-full-20261111';a=p/'audit'
sha=lambda b:hashlib.sha256(b).hexdigest()
b=(p/'out/kernel.macho').read_bytes();parse_kernel(b)
result=json.loads((p/'result.json').read_text());assert sha(b)==result['kernel_sha256']
meta=json.loads((p/'out/kernel.json').read_text());assert meta['candidate_sha256']==sha(b)
s=m.symbols(b); addresses=sorted(set(s.values())); reverse={v:k for k,v in s.items()}
layout=json.loads((a/'layout.json').read_text())
assert [layout[k] for k in ('max_cpus','max_clusters','cpu_entry_bytes','topology_cpu_bytes','topology_cluster_bytes','clutch_multiple_offset','clutch_multiple_bytes')]==[8,2,16,120,72,232,4]
def function(n):
    va=s[n];end=next(v for v in addresses if v>va);off=m.locate(b,va,executable=True)[1]
    ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-va],va))
    assert len(ins)*4==end-va,(n,len(ins)*4,end-va)
    text='\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}'+(' ; '+reverse.get(int(i.op_str[1:],16),'') if i.mnemonic in ('bl','b') and i.op_str.startswith('#0x') else '') for i in ins)+'\n'
    (a/(n+'.asm')).write_text(text)
    return ins,text
modules={}
for name,digest in meta['original_executable_sha256'].items():
    extent=next(e for e in meta['extents'] if e['module']==name and e['segment']=='__TEXT_EXEC')
    off=m.locate(b,extent['va'])[1];actual=sha(b[off:off+extent['size']]);assert actual==digest,name
    modules[name]=actual
assert len(modules)==30
_,reset=function('_reset_vector');assert 'and x0, x15, #0xffffff' in reset and 'add x3, x1, #0x80' in reset
_,lookup=function('_ml_get_cpu_number');assert 'and w8, w8, #0xffffff' in lookup
spans={n:next(v for v in addresses if v>s[n])-s[n] for n in ('_CpuDataEntries','_topology_cpu_array','_topology_cluster_array')}
assert spans=={'_CpuDataEntries':8*16,'_topology_cpu_array':8*120,'_topology_cluster_array':2*72},spans
off=m.locate(b,s['_sched_clutch_dispatch'])[1]
assert struct.unpack_from('<I',b,off+232)[0]==0
ins,init=function('__ZN6ArmCpu7initCPUEb')
# Derive the immediate constant used by the actual affinity XOR/test, rather
# than only matching a source macro that could be compiled out.
assert 'eor x8, x9, x8' in init and 'tst x8, x21' in init
register=None
for i in ins:
    if i.mnemonic=='mov' and i.op_str.startswith('x21, #'):register=int(i.op_str.split('#')[1],0)
    if i.mnemonic=='movk' and i.op_str.startswith('x21, #'):
        q=re.fullmatch(r'x21, #(0x[0-9a-f]+|\d+), lsl #(\d+)',i.op_str);assert q and register is not None
        value=int(q[1],0);shift=int(q[2]);register=(register&~(0xffff<<shift))|(value<<shift)
    if i.mnemonic=='tst' and i.op_str=='x8, x21':break
assert register==0xff00ffffff,hex(register)
assert all(x in init for x in ('ldxr w9, [x8]','add w9, w9, #1','stxr w10, w9, [x8]','cbnz w10,'))
armcpu={}
for n in s:
    if n.startswith(('__ZN6ArmCpu','__ZNK6ArmCpu')) and n not in ('__ZN6ArmCpu10gMetaClassE','__ZN6ArmCpu10superClassE','__ZN6ArmCpu9metaClassE'):
        ins,text=function(n);fp=[f'{i.mnemonic} {i.op_str}' for i in ins if re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str)]
        assert not fp,(n,fp)
        armcpu[n]={'address':hex(s[n]),'instructions':len(ins),'fp_simd':False}
source=json.loads((a/'source-comparison.json').read_text())
assert all(v['identical_lf_text'] for v in source['candidate_comparison'].values())
windows=json.loads((ROOT/'research/cpu-affinity-20261111/candidate-manifest.json').read_text())
for name in source['candidate_comparison']:
    assert sha((p/'sources'/name).read_text().replace('\r\n','\n').encode())==sha((ROOT/'research/cpu-affinity-20261111/candidate'/name).read_text().replace('\r\n','\n').encode()),name
external=json.loads((p/'external-build.json').read_text());objects=[]
private='/Users/ciscohe/Desktop/UniXNU_elden/research/cpu-full-20261111/'
for group,row in external['groups'].items():
    assert row['archive'].startswith(private+'archives/')
    for o in row['objects']:
        assert not any('/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/' in x or '/xnu-v77-records/' in x for x in o['command']),o['name']
        objects.append(o['name'])
assert len(objects)==41
loaded=sorted(n for n in s if '.a(' in n)
assert loaded and not any('/xnu-v77-records/' in n or '/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/' in n for n in loaded)
linkarchives=(a/'linkarchives.txt').read_text()
assert '/xnu-v77-records/' not in linkarchives and '/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/' not in linkarchives
assert all('-Wl,-force_load,'+row['archive'] in linkarchives for row in external['groups'].values())
# PmicTransport's six C objects intentionally have no -g; they create no
# archive STAB records. Check the actual linker record and their code exports.
assert all(any(row['archive']+'(' in n for n in loaded) for group,row in external['groups'].items() if group!='EldenPmicTransport')
transport_exports=['_qcom_mdt_parse','_qcom_smem_find','_qcom_smp2p_service','_qcom_glink_poll','_qcom_qmp_step','_qcom_rpmh_active_request']
for name in transport_exports:
    assert name in s,name
    m.locate(b,s[name],executable=True)
v77=ROOT/'edl_work/basic-bundle-20261108/v77/final/icon-cpu-focused-gestures-ipad-pro/boot-signed.img'
assert sha(v77.read_bytes())=='3bc5c53882085a208e5d9c8e3426e7adcced71bab1c1dd98fb70c6a0e4644e81'
report={'accepted_offline_cpu_layout':True,'kernel_sha256':sha(b),'original_modules':modules,'layout':layout,'linked_array_bytes':spans,
        'clutch_multiple_psets':False,'affinity_mask':hex(register),'online_increment':'ldxr/stxr retry loop',
        'armcpu_functions':armcpu,'rebuilt_external_objects':len(objects),'loaded_archives':loaded,
        'linkarchives_sha256':sha((a/'linkarchives.txt').read_bytes()),'transport_exports_present':transport_exports,
        'source_differences':'CRLF/LF only for exported selected CPU files','v77_image_unchanged':True,
        'boot_candidate_qualified':False,'hardware_tested':False,
        'limits':['Early secondary startup/recovery ordering unqualified','Timer/directed IPI/scheduling on Y700 secondary CPUs untested','Final boot integration and all original candidate checks remain required']}
(a/'linked-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('original_modules','armcpu_functions','loaded_archives','layout')},indent=2))
