"""Inspect the actual linked exec and init code, including inlined integration."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
p=argparse.ArgumentParser();p.add_argument('--version',type=int,default=62);v=p.parse_args().version
out=r/f'edl_work/basic-bundle-20261108/v{v}';b=(out/'final/kernel.macho').read_bytes();syms=m.symbols(b);ordered=sorted(set(syms.values()));reverse={a:n for n,a in syms.items()}
records={};allcalls=set()
baseline=(r/'edl_work/basic-bundle-20261108/v61/final/kernel.macho').read_bytes();bs=m.symbols(baseline)
def vector_ops(ins):return [i for i in ins if re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str)]
functions=['_exec_extract_strings','_load_init_program']
if v>=65:functions+=['_exec_activate_image','_elden_icon_cpu_user_marker' if v>=67 else '_elden_icon_cpu_status_worker']+[n for n in ['_elden_icon_cpu_file'] if n in syms]
if v>=70:functions+=['_process_signature']+[n for n in ['_elden_icon_cpu_owner_launch'] if n in syms]
if v>=71:functions += [n for n in ['_elden_icon_cpu_progress','_elden_icon_cpu_observe_process'] if n in syms]
if v>=72:functions+=['_kdebug_trace','_kdebug_trace64','_proc_prepareexit']
for n in functions:
 start=syms[n];end=next(a for a in ordered if a>start);off=m.locate(b,start)[1];ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(b[off:off+end-start],start));assert ins
 vectors=vector_ops(ins)
 if n in ('_exec_extract_strings','_elden_icon_cpu_status_worker','_elden_icon_cpu_user_marker','_elden_icon_cpu_owner_launch','_elden_icon_cpu_progress','_elden_icon_cpu_observe_process'):assert not vectors,n
 else:
  # kern_exec is normal kernel context; its qualified baseline already uses
  # vector loads/stores for structure copies. Do not report it as no-SIMD.
  baseline_name=n if n in bs else '_load_init_program'
  bs0=bs[baseline_name];be=min(a for a in bs.values() if a>bs0);bo=m.locate(baseline,bs0)[1]
  oldvec=vector_ops(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(baseline[bo:bo+be-bs0],bs0))
  if vectors and n not in ('_kdebug_trace','_kdebug_trace64','_proc_prepareexit'):assert oldvec and {i.mnemonic for i in vectors}<={i.mnemonic for i in oldvec}
 lines=[];calls=[]
 for i in ins:
  label=''
  if i.mnemonic in ('bl','b') and i.op_str.startswith('#0x'):
   label=reverse.get(int(i.op_str[1:],16),'');calls.append(label)
  lines.append(f'{i.address:x}: {i.mnemonic} {i.op_str}'+(' ; '+label if label else ''))
 (out/(n+'.asm')).write_text('\n'.join(lines)+'\n');allcalls.update(calls)
 records[n]=dict(start=hex(start),bytes=end-start,instructions=len(ins),fp_simd=bool(vectors),vector_instructions=len(vectors),sha256=hashlib.sha256(b[off:off+end-start]).hexdigest(),calls=sorted(set(calls)-{''}))
 if n=='_load_init_program':records[n]['baseline_vector_instructions']=len(oldvec);records[n]['scope']='normal kern_exec structure copies; not a no-FP driver routine'
assert {'_exec_add_user_string','_vnode_open','_vn_rdwr','_vnode_setattr'}<=allcalls,allcalls
asset=r/('edl_work/icon-dependency-20261111' if v>=65 else 'edl_work/icon-cpu-20261111')
if v>=65 and (asset/f'v{v}').is_dir():asset=asset/f'v{v}'
lib=(asset/'EldenIconCpuMode.dylib').read_bytes();offset=m.locate(b,syms['_elden_icon_cpu_payload'])[1];assert b[offset:offset+len(lib)]==lib
if v>=65:
 agent=(asset/'iconservicesagent').read_bytes();offset=m.locate(b,syms['_elden_icon_agent_payload'])[1];assert b[offset:offset+len(agent)]==agent
src=r/f'research/basic-bundle-20261108/source-compiled-v{v}/xnu';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(accepted=True,kernel_sha256=hashlib.sha256(b).hexdigest(),env_sha256=sha(src/'EldenIconCpuEnv.inc'),stage_sha256=sha(src/'EldenIconCpuStage.inc'),payload_sha256=hashlib.sha256(lib).hexdigest(),functions=records,hardware_tested=False)
if v>=65:report.update(common_sha256=sha(src/'EldenIconCpuCommon.inc'),paths_sha256=sha(src/'EldenIconCpuPaths.h'))
if v>=67:report['marker_no_simd']=True
elif v>=65:report['status_worker_no_simd']=True
if v>=72:
 assert '_elden_icon_cpu_user_marker' in records['_kdebug_trace64']['calls'],'marker not reachable from syscall179'
 assert {'_elden_icon_cpu_user_marker','_kdebug_trace64'} & set(records['_kdebug_trace']['calls']),'marker not reachable from syscall180'
 assert '_elden_icon_cpu_exit_selected' in records['_proc_prepareexit']['calls'],'exit observer not compiled'
 report['live_entry_observers_verified']=True
(out/'icon-codegen.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'accepted':True,'functions':{n:v['instructions'] for n,v in records.items()},'payload_bytes':len(lib)},indent=2))
