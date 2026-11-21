"""Link the camera capture session composition over the retained full parent."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
from camera_irq_input_audit import retained_camera_irq_input
from audit_camera_session_link import approved_replacement
ROOT=Path(__file__).resolve().parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())

REPLACED=(('NativePipeline.o','pipeline_build'),('FrameOutput.o','frames_build'),('Service.o','frames_build'))
EQUIVALENT=('CameraPipeline.o','CaptureBinding.o')   # rebuilt peers; byte-identical or rejected
SESSION_OBJECT='CaptureSession.o'

def prepare(parent,pipeline_build,frames_build,session_build):
    """Validate every qualified build receipt and compose the exact session
    link command: all retained parent inputs byte-identical, the three camera
    objects replaced by their receipt-bound builds, one CaptureSession.o added."""
    prior=read(parent/'result.json');pipeline=read(pipeline_build/'result.json')
    frames=read(frames_build/'result.json');session=read(session_build/'result.json')
    if not all(r['passed'] for r in (prior,pipeline,frames)) or frames['parent_kernel_sha256']!=prior['kernel_sha256']:
        raise ValueError('Qualified parent/native camera builds required')
    if frames.get('adjacent_pipeline_build_sha256')!=sha(pipeline_build/'result.json'):
        raise ValueError('Frame/native IRQ ABI not source-bound')
    if session.get('capture_session_object')!=SESSION_OBJECT or session.get('automatic_startup') is not False:
        raise ValueError('Session build profile absent')
    for n in ('kernel','native'):
        if sha(parent/(n+'.macho'))!=prior[n+'_sha256']:raise ValueError('Parent image changed')
    for folder,rows in ((pipeline_build,pipeline['native_objects']),(frames_build,frames['objects'])):
        for name,row in rows.items():
            path=folder/name
            if sha(path)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(path)):
                raise ValueError('Actual native candidate changed/auto-start')
    session_obj=session_build/SESSION_OBJECT
    if not session_obj.is_file() or sha(session_obj)!=session['object_sha256']:
        raise ValueError('Actual camera session object changed')
    if any(n.endswith('/__mod_init_func') for n in sections(session_obj)):
        raise ValueError('Camera session object carries automatic startup')
    command=read(parent/'link-command.json');original=list(command);inputs=dict(prior['inherited_inputs']);deltas={};builds={}
    for value in command:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inputs[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inputs[line]=sha(Path(line))
    if any(Path(v.split(',')[-1]).name==SESSION_OBJECT for v in command):
        raise ValueError('Camera session object already a link input')
    folders={'pipeline_build':pipeline_build,'frames_build':frames_build}
    records={'pipeline_build':pipeline,'frames_build':frames};insert=None
    for name,kind in REPLACED:
        hits=[i for i,v in enumerate(command) if Path(v).name==name]
        if len(hits)!=1:raise ValueError('Unique original camera object required: '+name)
        before=Path(command[hits[0]]);after=folders[kind]/name;digest=sha(folders[kind]/'result.json')
        deltas[name]={'before_path':str(before),'before':sha(before),'after_path':str(after),'after':sha(after),'source_build_sha256':digest}
        builds[name]=digest;inputs[str(after)]=sha(after);command[hits[0]]=str(after)
        insert=hits[0]
    command.insert(insert+1,str(session_obj));inputs[str(session_obj)]=sha(session_obj)
    equivalences={}
    for name in EQUIVALENT:
        before=[Path(v) for v in original if Path(v).name==name]
        if len(before)!=1:raise ValueError('Unique actual retained camera peer object required')
        rebuilt=pipeline_build/name
        if sha(before[0])!=sha(rebuilt):raise ValueError('Unrelated camera object changed: '+name)
        equivalences[name]={'retained_path':str(before[0]),'retained_sha256':sha(before[0]),
            'recompiled_path':str(rebuilt),'recompiled_sha256':sha(rebuilt),'identical_bytes':True}
    return command,inputs,deltas,builds,equivalences,prior,pipeline,frames,session,session_obj

def verify_retained(inputs):
    for name,digest in inputs.items():
        if sha(Path(name))!=digest:raise ValueError('Inherited actual input changed')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('parent','pipeline-build','frames-build','session-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args()
 for n in ('parent','pipeline_build','frames_build','session_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 command,inputs,deltas,builds,equivalences,prior,pipeline,frames,session,session_obj=prepare(
     a.parent,a.pipeline_build,a.frames_build,a.session_build)
 command[command.index('-o')+1]=str(out/'native.macho');(out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
 def run(cmd,label):
  with (out/(label+'.log')).open('x') as stream:r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=stream,stderr=subprocess.STDOUT)
  if r.returncode:raise RuntimeError(label+' failed; exact log retained')
 run(command,'native-link')
 verify_retained(inputs)
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
  'camera_session_build_sha256':sha(a.session_build/'result.json'),
  'camera_session_object':{'path':str(session_obj),'sha256':session['object_sha256']},
  'actual_camera_capture_session_link':True,'physical_camera_capture_session':False,
  'physical_camera_IRQ_frames':False,'automatic_camera_executor':False,'automatic_startup':False,
  'boot_packaged':False,'hardware':False,
  'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
 for name in ('NativePipeline.o','FrameOutput.o'):retained_camera_irq_input(deltas[name]['before_path'],command,report)
 approved_replacement(Path(deltas['Service.o']['before_path']),command,report)
 if report['aggregate_recovery_bound_ms']!=5500 or report['recovery_object_sha256']!=prior['recovery_object_sha256']:
  raise ValueError('Original recovery changed')
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS exact camera session full link: three receipt-bound replacements, one session object; recovery/Apple30 preserved')

if __name__=='__main__':main()
