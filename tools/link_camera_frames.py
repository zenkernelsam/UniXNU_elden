"""Link cached real-frame snapshots/OS17 service over sharedSMMU parent."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
from replay_xnu_objects import sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('parent','frames-build','prelink-tool','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args()
 for n in ('parent','frames_build','prelink_tool','original_kernelcache'):setattr(a,n,getattr(a,n).resolve(strict=True))
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);r=json.loads((a.parent/'result.json').read_text());built=json.loads((a.frames_build/'result.json').read_text())
 if not r['passed'] or not built['passed'] or built['parent_kernel_sha256']!=r['kernel_sha256']:raise ValueError('Unqualified parent/frames')
 for n in ('kernel','native'):
  if sha(a.parent/(n+'.macho'))!=r[n+'_sha256']:raise ValueError('Parent identity changed')
 cmd=json.loads((a.parent/'link-command.json').read_text());inputs={}
 for v in cmd:
  path=Path(v.split(',')[-1])
  if path.is_file() and path.suffix in ('.a','.o'):inputs[str(path)]=sha(path)
  if path.is_file() and path.name.endswith('filelist'):
   for name in path.read_text().splitlines():inputs[name]=sha(Path(name))
 for name,row in built['objects'].items():
  f=a.frames_build/name
  if sha(f)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(f)):raise ValueError('New frame object changed/auto-init')
  if any(Path(v).name==name for v in cmd):raise ValueError('Duplicate real-frame object')
  cmd.append(str(f))
 cmd[cmd.index('-o')+1]=str(out/'native.macho');(out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
 def run(command,label):
  with (out/(label+'.log')).open('x') as f:p=subprocess.run(list(map(str,command)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(label+' failed; original log retained')
 run(cmd,'native-link')
 for n,h in inputs.items():
  if sha(Path(n))!=h:raise ValueError('Original peer input mutated')
 prior=json.loads((a.parent/'kernel.json').read_text())
 if sha(a.original_kernelcache)!=prior['original_sha256']:raise ValueError('Original OS17 kernel changed')
 pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];args=[sys.executable,a.prelink_tool/'emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,'--slide='+str(prior['slide']),'--output',out/'kernel.macho']
 for n in pre['roots']:args+=['--root',n]
 for n in pre['early_security_initializers']:args+=['--early-security-init',n]
 run(args,'prelink');meta=json.loads((out/'kernel.json').read_text());count=verify_original_executables(out/'kernel.macho',meta,prior['original_executable_sha256'])
 inherited=dict(r['inherited_inputs']);inherited.update(inputs);report=dict(r)
 report.update({'passed':True,'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'parent_kernel_sha256':r['kernel_sha256'],
   'original_regions_preserved':count,'inherited_inputs':inherited,'camera_frames_build_sha256':sha(a.frames_build/'result.json'),'camera_frames_objects':built['objects'],
   'actual_camera_cached_snapshots_service':True,'automatic_frame_capture_or_service':False,'physical_camera_frames':False,'boot_packaged':False,'hardware':False,
   'legacy_flags_unchanged':{n:meta.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}})
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS actual frame/snapshot/read-onlyservice full link; all parent archives/recovery/Apple30 retained')
if __name__=='__main__':main()
