"""Audit actual original-worker camera IRQ code, wake route and strict deltas."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from camera_irq_input_audit import retained_camera_irq_input,equivalent_service
from peripheral_archive_audit import retained_archive
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('kernel-build','parent','pipeline-build','frames-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
 r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text())
 pipeline=json.loads((a.pipeline_build/'result.json').read_text());frames=json.loads((a.frames_build/'result.json').read_text())
 if r.get('actual_original_camera_IRQ_executor') is not True or set(r.get('camera_irq_direct_deltas',{}))!={'NativePipeline.o','FrameOutput.o'}:
  raise ValueError('Exact camera IRQ profile/object scope absent')
 for folder,record in ((a.kernel_build,r),(a.parent,prior)):
  for n in ('kernel','native'):
   if sha(folder/(n+'.macho'))!=record[n+'_sha256']:raise ValueError('Linked source identity changed')
 if sha(a.pipeline_build/'result.json')!=r['camera_irq_builds']['NativePipeline.o'] or sha(a.frames_build/'result.json')!=r['camera_irq_builds']['FrameOutput.o']:
  raise ValueError('Camera source build identity changed')
 before=json.loads((a.parent/'link-command.json').read_text());after=json.loads((a.kernel_build/'link-command.json').read_text())
 transformed=list(before)
 for row in r['camera_irq_direct_deltas'].values():
  retained_camera_irq_input(row['before_path'],after,r)
  transformed[transformed.index(row['before_path'])]=row['after_path']
 transformed[transformed.index('-o')+1]=after[after.index('-o')+1]
 if transformed!=after:raise ValueError('Unexpected full-link input change outside the two exact camera methods')
 for value in before:
  path=Path(value.split(',')[-1])
  if value.startswith('-Wl,-force_load,'):retained_archive(value,after,r)
  elif path.is_file() and path.suffix in ('.a','.o') and path.name not in ('NativePipeline.o','FrameOutput.o'):
   if sha(path)!=r['inherited_inputs'].get(str(path)):raise ValueError('Retained original native object changed')
 eq=r['camera_irq_equivalent_objects']['Service.o']
 if equivalent_service(eq['retained_path'],eq['recompiled_path'])!=eq:raise ValueError('Actual unchanged service differs')

 raw=(a.kernel_build/'kernel.macho').read_bytes();im=macho(raw);names={n:v for v,g in exports(raw,im).items() for n in g}
 local={};starts=[]
 for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
  m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
  if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
 def symbol(n):
  values=local.get(n,set()).copy()
  if n in names:values.add(names[n])
  if len(values)!=1:raise ValueError('Unique camera IRQ symbol missing: '+n)
  return values.pop()
 calls=[]
 for segment in im['segments']:
  for sec in segment['sections']:
   if sec['name']!='__text':continue
   data=raw[sec['fileoff']:sec['fileoff']+sec['size']]
   for off in range(0,len(data)-3,4):
    word=struct.unpack_from('<I',data,off)[0]
    if word&0x7c000000==0x14000000:
     pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
 def inside(caller):
  first=symbol(caller);last=min((pc for pc,n in starts if pc>first),default=None)
  if last is None:raise ValueError('Actual method extent missing')
  return [target for pc,target in calls if first<=pc<last and not first<=target<last]
 def edge(caller,callee):
  if symbol(callee) not in inside(caller):raise ValueError('Actual camera IRQ call missing: '+caller+'→'+callee)
 required=[('_elden_camera_output_enable_irq','_elden_camera_native_pipeline_listen'),
  ('_elden_camera_output_pump_irq','_elden_camera_native_pipeline_wait'),
  ('_elden_camera_output_pump_irq','_elden_camera_output_pump'),
  ('_elden_camera_native_pipeline_wait','_assert_wait_timeout'),
  ('_elden_camera_native_pipeline_wait','_clear_wait'),
  ('_elden_camera_native_pipeline_wait','_thread_block'),
  ('_elden_camera_native_pipeline_service','_elden_camera_pipeline_service')]
 for caller,callee in required:edge(caller,callee)
 irq_names=[n for n in local if re.match(r'__ZN12_GLOBAL__N_112pipeline_irqEPv$',n)]
 if len(irq_names)!=1:raise ValueError('Actual primary camera IRQ handler missing')
 irq=irq_names[0];targets=inside(irq);allowed={symbol('_armcpu_spi_mask'),symbol('_thread_wakeup_prim')}
 if not targets or not set(targets)<=allowed or not allowed<=set(targets):
  raise ValueError('Camera primary IRQ calls a sleepable/physical/unapproved method')
 for name in ('_elden_camera_output_enable_irq','_elden_camera_output_pump_irq'):
  if any(t==symbol(name) for pc,t in calls):raise ValueError('Unqualified automatic outer camera IRQ startup')
 source=(ROOT/'drivers/EldenCamera/NativePipelineMethods.inc').read_text()
 wait=source.split('extern "C" int elden_camera_native_pipeline_wait',1)[1].split('extern "C" int',1)[0]
 enrolled=wait.split('assert_wait_timeout(',1)[1].split('else thread_block',1)[0]
 enrolled=re.sub(r'//[^\n]*','',enrolled)
 if any(name in enrolled for name in ('pipeline_admit','IOLock','block[','gic.','provider.')):
  raise ValueError('Sleepable/provider operation after original wait enrollment')
 fixed=json.loads((ROOT/'drivers/EldenCamera/pipeline-source.json').read_text())
 dts=ROOT/'drivers/EldenCamera/upstream/sm8750.dtsi'
 row=next(x for x in fixed['files'] if x['file']==dts.name)
 if sha(dts)!=row['sha256'] or fixed['commit']!='6f97250ead155fc3b980856323e99a4623dae73d':raise ValueError('Exact Gen4 DTS input changed')
 text=dts.read_text().replace('\r\n','\n');section=text.split('camss: camss@',1)[1] if 'camss: camss@' in text else text[text.index('interrupts = <GIC_SPI 601'):]
 specs=re.findall(r'<GIC_SPI (\d+) (IRQ_TYPE_[A-Z_]+)>',section.split('interrupt-names',1)[0])
 if not specs or any(kind!='IRQ_TYPE_EDGE_RISING' for _,kind in specs):raise ValueError('Exact CAMSS edge source differs')
 if r['recovery_object_sha256']!=prior['recovery_object_sha256'] or r['aggregate_recovery_bound_ms']!=5500:
  raise ValueError('Original recovery changed')
 a.output.write_text(json.dumps({'passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],
  'exact_two_object_replacement':True,'all_other_link_inputs_unchanged':True,'equivalent_retained_service':eq,
  'actual_primary_irq_symbol':irq,'primary_irq_only_mask_wake':True,'actual_call_edges':required,
  'post_assert_wait_only_atomic_peeks':True,'fixed_CAMSS_DTS_edges':specs,'hardware':False,
  'automatic_executor':False,'physical_frames_verified':False},indent=2)+'\n')
 print('PASS actual camera primary IRQ only mask/wake, originalworker wait/service, strict2objects, unchanged recovery')

if __name__=='__main__':main()
