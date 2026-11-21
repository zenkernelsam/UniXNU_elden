"""Build scalar OS17 real-frame snapshots/IOService and software RAW10 object."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('source','base-native','llvm','parent','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--pipeline-build',type=Path,help='Source-qualified adjacent IRQ pipeline providing the new original-worker methods')
 a=p.parse_args()
 for n in ('source','base_native','llvm','parent'):setattr(a,n,getattr(a,n).resolve(strict=True))
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
 for n,d in record['sources'].items():
  if sha(a.source/n)!=d:raise ValueError('Prepared frame source changed')
 def run(args,label,cwd=None):
  r=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(label+' failed; original log retained')
  return r.stdout.decode()
 cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
 old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project'
 remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
 for n,row in cap['files'].items():
  if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Original native headers changed')
 flags=[];i=0
 while i<len(job['arguments']):
  v=job['arguments'][i]
  if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
  if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
  flags.append(remap(v));i+=1
 objects={};commands={}
 for name in ('FrameOutput','Service'):
  obj=out/(name+'.o');raw=out/(name+'.raw.o') if name=='Service' else obj
  cmd=[*flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/(name+'.cpp'),'-o',raw]
  run(cmd,name+'-build',remap(job['directory']));commands[obj.name]=list(map(str,cmd))
  if name=='Service':
   run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_Service.cpp',raw,obj],'late-metaclasses')
   if sections(raw)['__TEXT/__text']!=sections(obj)['__TEXT/__text']:raise ValueError('Manual metaclass transformation changed code')
  asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm');ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
  if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('New native frame service must remain scalar')
  if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic camera startup')
  syms=global_symbols(obj);objects[obj.name]={'sha256':sha(obj),'imports':[n for n,v in syms.items() if not v[0]&0xe],'exports':[n for n,v in syms.items() if v[0]&0xe],
      'warnings':(out/(name+'-build.log')).read_text().count('warning:')}
 # CPU conversion is userland; never perform software ISP on the kernel worker.
 run(['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'Raw10.c','-o',out/'Raw10.user.o'],'raw10-user-build')
 available=set()
 for line in subprocess.check_output(['xcrun','nm','-a',a.parent/'native.macho'],text=True).splitlines():
  m=re.match(r'^[0-9a-fA-F]+\s+[sStTdDbB]\s+(\S+)$',line)
  if m:available.add(m[1])
 adjacent=None
 if a.pipeline_build:
  folder=a.pipeline_build.resolve(strict=True);adjacent=json.loads((folder/'result.json').read_text())
  if not adjacent['passed'] or set(adjacent['native_objects'])!={'CameraPipeline.o','NativePipeline.o','CaptureBinding.o'}:
   raise ValueError('Unqualified adjacent native pipeline')
  source=folder.parent/('source'+folder.name.removeprefix('native'));source_record=source/'source.json'
  if sha(source_record)!=adjacent['source_record_sha256'] or sha(source/'NativePipeline.h')!=sha(a.source/'NativePipeline.h'):
   raise ValueError('Frame and adjacent IRQ pipeline source ABI differs')
  for name,row in adjacent['native_objects'].items():
   obj=folder/name
   if sha(obj)!=row['sha256'] or any(n.endswith('/__mod_init_func') for n in sections(obj)):
    raise ValueError('Adjacent native pipeline object changed or auto-starts')
   available.update(n for n,v in global_symbols(obj).items() if v[0]&0xe)
 defined={n for row in objects.values() for n in row['exports']};used={n for row in objects.values() for n in row['imports']}
 if used-defined-available:raise ValueError('Real frame native provider unresolved: '+str(used-defined-available))
 result={'passed':True,'objects':objects,'compile_commands':commands,'cwd':remap(job['directory']),'source_record_sha256':sha(a.source/'source.json'),
     'software_raw10_user_object_sha256':sha(out/'Raw10.user.o'),'parent_kernel_sha256':sha(a.parent/'kernel.macho'),
     'automatic_startup':False,'physical_provider_installed':False,'hardware':False,'full_link':False}
 if adjacent:result['adjacent_pipeline_build_sha256']=sha(a.pipeline_build/'result.json')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS2scalarOS17 retained frames/read-only service objects and userland full10bit preview; explicit late classes')
if __name__=='__main__':main()
