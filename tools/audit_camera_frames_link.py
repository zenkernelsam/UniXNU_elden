"""Audit actual real-frame snapshots, service ABI/manual classes and parent."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from camera_irq_input_audit import retained_camera_irq_input,equivalent_service
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('kernel-build','parent','frames-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
 r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text());build=json.loads((a.frames_build/'result.json').read_text())
 if r.get('actual_camera_cached_snapshots_service') is not True or sha(a.frames_build/'result.json')!=r['camera_frames_build_sha256']:raise ValueError('Actual frame profile absent')
 for folder,record in ((a.kernel_build,r),(a.parent,prior)):
  if sha(folder/'kernel.macho')!=record['kernel_sha256'] or sha(folder/'native.macho')!=record['native_sha256']:raise ValueError('Frame kernel/native differs')
 raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
 for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
  m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
  if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
 def symbol(n):
  v=local.get(n,set()).copy()
  if n in names:v.add(names[n])
  if len(v)!=1:raise ValueError('Unique real-frame symbol missing: '+n)
  return v.pop()
 calls=[];init=[];previous=[]
 for image,body,table in ((im,raw,init),(oi,old,previous)):
  for seg in image['segments']:
   for sec in seg['sections']:
    b=body[sec['fileoff']:sec['fileoff']+sec['size']]
    if sec['name']=='__mod_init_func':table+=list(struct.unpack('<%dQ'%(len(b)//8),b))
    if image is im and sec['name']=='__text':
     for off in range(0,len(b)-3,4):
      word=struct.unpack_from('<I',b,off)[0]
      if word&0x7c000000==0x14000000:pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
 if len(init)!=len(previous):raise ValueError('New automatic frame/service initializer')
 for n in ('_elden_camera_output_open','_elden_camera_service_publish','_elden_camera_service_withdraw'):
  if any(t==symbol(n) for pc,t in calls):raise ValueError('Unqualified camera frame/service outer startup')
 pump_hits=[pc for pc,t in calls if t==symbol('_elden_camera_output_pump')]
 if r.get('actual_original_camera_IRQ_executor') is True:
  caller=symbol('_elden_camera_output_pump_irq');end=min((pc for pc,_ in starts if pc>caller),default=None)
  if not pump_hits or end is None or any(not caller<=pc<end for pc in pump_hits):raise ValueError('Frame pump caller outside actual IRQ wrapper')
  for n in ('_elden_camera_output_enable_irq','_elden_camera_output_pump_irq'):
   if any(t==symbol(n) for pc,t in calls):raise ValueError('Unqualified outer camera IRQ executor start')
 elif pump_hits:raise ValueError('Unqualified original frame pump caller')
 late=symbol('__GLOBAL__sub_I_Service.cpp')
 if late in init or len([pc for pc,t in calls if t==late])!=1:raise ValueError('Exactly one explicit late class registration required')
 # Verify original IOService/IOUserClient inheritance, exact overrides and
 # loaded native-method bytes. No captured fake vtable slots are adopted.
 def u64(at):return struct.unpack_from('<Q',raw,backed_offset(im['segments'],at,8,len(raw)))[0]
 def table(n):
  out=[];base=symbol(n)+16
  for i in range(512):
   v=u64(base+i*8)
   if not v:return out
   out.append(v)
  raise ValueError('Native method table boundary missing')
 classes={}
 for child,parent,expected in (('17EldenCameraClient','12IOUserClient',8),('18EldenCameraService','9IOService',6)):
  derived=table('__ZTV'+child);original=table('__ZTV'+parent)
  if len(derived)!=len(original) or u64(symbol('__ZN'+child+'10superClassE'))!=symbol('__ZN'+parent+'10gMetaClassE'):raise ValueError('Original camera service inheritance differs')
  changed=[i for i in range(len(original)) if original[i]!=derived[i]]
  if len(changed)!=expected:raise ValueError('Unexpected camera service overrides')
  for i in changed:
   matches=[n for n,v in local.items() if derived[i] in v and (n.startswith('__ZN'+child) or n.startswith('__ZNK'+child))]
   if not matches:raise ValueError('Unexpected frame-service method owner')
  classes[child]={'slots':len(original),'overrides':changed}
 required=['_elden_camera_dma_capture_service','_elden_camera_dma_capture_frame','_elden_camera_dma_capture_release','_elden_camera_native_pipeline_queue',
  '_elden_camera_dma_admit','_elden_camera_dma_status','_elden_camera_native_pipeline_status','_elden_camera_output_acquire','_elden_camera_output_release','_elden_camera_output_memory',
  '_elden_camera_output_info','__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm','__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv','__ZN25IOGeneralMemoryDescriptor7prepareEj']
 edges={}
 for n in required:
  hits=[pc for pc,t in calls if t==symbol(n)]
  if not hits:raise ValueError('Actual frame DMA/copy/return/service edge absent: '+n)
  edges[n]={'target':hex(symbol(n)),'calls_and_tails':[hex(pc) for pc in hits]}
 before=json.loads((a.parent/'link-command.json').read_text());after=json.loads((a.kernel_build/'link-command.json').read_text());archives={}
 for v in before:
  path=Path(v.split(',')[-1])
  if v.startswith('-Wl,-force_load,'):archives[str(path)]=retained_archive(v,after,r)
  elif path.is_file() and path.suffix in ('.a','.o'):
   if r.get('actual_original_camera_IRQ_executor') is True and path.name in ('NativePipeline.o','FrameOutput.o'):
    retained_camera_irq_input(path,after,r);continue
   if after.count(v)!=1 or sha(path)!=r['inherited_inputs'].get(str(path)):raise ValueError('Original peer native object changed')
 for n,row in build['objects'].items():
  if r.get('actual_original_camera_IRQ_executor') is True and n=='Service.o':
   eq=r.get('camera_irq_equivalent_objects',{}).get(n)
   if not isinstance(eq,dict) or eq['recompiled_path']!=str((a.frames_build/n).resolve()) or row['sha256']!=eq['recompiled_sha256']:
    raise ValueError('Exact equivalent service source-build receipt absent')
   if equivalent_service(eq['retained_path'],eq['recompiled_path'])!=eq or after.count(eq['retained_path'])!=1:
    raise ValueError('Retained original Service differs from source rebuild')
   continue
  if sha(a.frames_build/n)!=row['sha256'] or after.count(str((a.frames_build/n).resolve()))!=1:raise ValueError('New frame object changed/duplicate')
 if r['recovery_object_sha256']!=prior['recovery_object_sha256'] or r['aggregate_recovery_bound_ms']!=5500:raise ValueError('Original peripheral recovery changed')
 flags=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:flags.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
 if any(v is not False for v in flags.values()):raise ValueError('Global qualification changed')
 report={'passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'actual_calls':edges,'classes':classes,'archives':archives,
  'original4K_and_recovery_retained':True,'explicit_late_metaclasses':True,'read_only_snapshot_service':True,'global_flags':flags,'physical_frames_camera_OS_acceptance':False,'hardware':False}
 a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS real-frame/snapshot/service native edges/ABI/original parent/recovery/Apple30, no auto-camera startup')
if __name__=='__main__':main()
