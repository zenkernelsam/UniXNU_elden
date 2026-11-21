"""Replace exact native camera IRQ/frame methods over the retained full parent."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
from camera_irq_input_audit import retained_camera_irq_input,equivalent_service
ROOT=Path(__file__).resolve().parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('parent','pipeline-build','frames-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args()
 for n in ('parent','pipeline_build','frames_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 prior=read(a.parent/'result.json');pipeline=read(a.pipeline_build/'result.json');frames=read(a.frames_build/'result.json')
 if not all(r['passed'] for r in (prior,pipeline,frames)) or frames['parent_kernel_sha256']!=prior['kernel_sha256']:
  raise ValueError('Qualified parent/native camera builds required')
 if frames.get('adjacent_pipeline_build_sha256')!=sha(a.pipeline_build/'result.json'):
  raise ValueError('Frame/native IRQ ABI not source-bound')
 for n in ('kernel','native'):
  if sha(a.parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
 for folder,rows in ((a.pipeline_build,pipeline['native_objects']),(a.frames_build,frames['objects'])):
  for name,row in rows.items():
   path=folder/name
   if sha(path)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(path)):
    raise ValueError('Actual native candidate changed/auto-start')
 command=read(a.parent/'link-command.json');original=list(command);inputs=dict(prior['inherited_inputs']);deltas={};builds={}
 for value in command:
  path=Path(value.split(',')[-1])
  if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
  if path.is_file() and path.name.endswith('filelist'):
   for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
 for name,folder,record in (('NativePipeline.o',a.pipeline_build,pipeline),('FrameOutput.o',a.frames_build,frames)):
  hits=[i for i,v in enumerate(command) if Path(v).name==name]
  if len(hits)!=1:raise ValueError('Unique original camera object required: '+name)
  before=Path(command[hits[0]]);after=folder/name;digest=sha(folder/'result.json')
  deltas[name]={'before_path':str(before),'before':sha(before),'after_path':str(after),'after':sha(after),'source_build_sha256':digest}
  builds[name]=digest;inputs[str(after)]=sha(after);command[hits[0]]=str(after)
 # The other rebuilt objects must be exactly equivalent to the retained inputs.
 # An unchanged object is never replaced just because it was recompiled.
 equivalences={}
 for name,folder in (('CameraPipeline.o',a.pipeline_build),('CaptureBinding.o',a.pipeline_build),('Service.o',a.frames_build)):
  before=[Path(v) for v in original if Path(v).name==name]
  if len(before)!=1:raise ValueError('Unique actual retained camera peer object required')
  if name=='Service.o':equivalences[name]=equivalent_service(before[0],folder/name)
  elif sha(before[0])!=sha(folder/name):raise ValueError('Unrelated camera object changed: '+name)
 command[command.index('-o')+1]=str(out/'native.macho');(out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
 def run(cmd,label):
  with (out/(label+'.log')).open('x') as stream:r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=stream,stderr=subprocess.STDOUT)
  if r.returncode:raise RuntimeError(label+' failed; exact log retained')
 run(command,'native-link')
 for name,digest in inputs.items():
  if sha(Path(name))!=digest:raise ValueError('Inherited actual input changed')
 parent_meta=read(a.parent/'kernel.json')
 if sha(a.original_kernelcache)!=parent_meta['original_sha256']:raise ValueError('Original OS17 kernel changed')
 plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
 cmd=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
      '--slide='+str(parent_meta['slide']),'--output',out/'kernel.macho']
 for n in plan['roots']:cmd+=['--root',n]
 for n in plan['early_security_initializers']:cmd+=['--early-security-init',n]
 run(cmd,'prelink');meta=read(out/'kernel.json')
 regions=verify_original_executables(out/'kernel.macho',meta,parent_meta['original_executable_sha256'])
 report=dict(prior);report.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
  'parent_kernel_sha256':prior['kernel_sha256'],'inherited_inputs':inputs,'original_regions_preserved':regions,
  'actual_original_camera_IRQ_executor':True,'camera_irq_direct_deltas':deltas,'camera_irq_builds':builds,
  'camera_irq_equivalent_objects':equivalences,
  'camera_irq_pipeline_source_record_sha256':pipeline['source_record_sha256'],
  'camera_frames_build_sha256':sha(a.frames_build/'result.json'),'camera_frames_objects':frames['objects'],
  'camera_frames_original_source_build_sha256':prior['camera_frames_build_sha256'],
  'physical_camera_IRQ_frames':False,'automatic_camera_executor':False,'boot_packaged':False,'hardware':False,
  'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
 for row in deltas.values():retained_camera_irq_input(row['before_path'],command,report)
 if report['aggregate_recovery_bound_ms']!=5500 or report['recovery_object_sha256']!=prior['recovery_object_sha256']:
  raise ValueError('Original recovery changed')
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS exact two-object camera IRQ/originalworker/snapshot full link; other inputs/recovery/Apple30 preserved')

if __name__=='__main__':main()
