"""Qualify the complete GPU runtime against a previously source-built kernel.

This incremental link is a gate before the final fresh complete build. Every
selected GPU object is hashed; original Apple instructions are preserved.
No unresolved-symbol relaxation, boot packaging or device operation occurs.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
from replay_xnu_objects import sections,global_symbols
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('tree','base','core','runtime','llvm','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--recovery',type=Path,required=True)
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 def run(cmd,name,cwd=None):
  with (out/(name+'.log')).open('xb') as f:r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
  if r.returncode:raise RuntimeError(name+' failed; see '+str(out))
 core=json.loads((a.core/'result.json').read_text());runtime=json.loads((a.runtime/'result.json').read_text())
 if not core['passed'] or not runtime['compiled']:raise ValueError('Incomplete GPU inputs')
 objects={}
 for name,row in core['sources'].items():
  src=a.core/(Path(name).stem+'.o')
  if sha(src)!=row['object_sha256']:raise ValueError('Core object identity changed')
  objects[src.name]=src
 for name,row in runtime['units'].items():
  if name=='firmware':src=a.runtime/'EldenGpuFirmware.o';digest=row['sha256']
  else:src=a.runtime/(Path(name).stem+'.o');digest=row['object_sha256']
  if sha(src)!=digest:raise ValueError('Runtime object identity changed')
  objects[src.name]=src
 # Late metaclasses are invoked explicitly only after their parents exist.
 for name in ('NativeAdrenoAccelerator','NativeGOPPresenter'):
  original=objects[name+'.o'];target=out/(name+'.late.o')
  run([a.llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func',
       '--globalize-symbol=__GLOBAL__sub_I_'+name+'.cpp',original,target],name+'-late')
  if sections(original)['__TEXT/__text']!=sections(target)['__TEXT/__text']:raise ValueError('Late conversion changed code')
  if any(k.endswith('/__mod_init_func') for k in sections(target)):raise ValueError('Late initializer section was not removed')
  entry='__GLOBAL__sub_I_'+name+'.cpp'
  if entry not in global_symbols(target) or not global_symbols(target)[entry][0]&0xe:raise ValueError('Late initializer is not exported')
  objects[name+'.o']=target
 includes=[a.tree/'kits/ArmSmmu4K',a.tree/'kits/AdrenoPM4',a.tree/'kits/AdrenoFirmware',a.tree/'kits/Sm8750Platform']
 sources=[]
 for directory,pattern in [(includes[0],'ArmSmmu*.c'),(includes[2],'Adreno*.c'),(includes[3],'Sm8750*.c')]:sources+=sorted(directory.glob(pattern))
 sources += [includes[1]/'AdrenoPM4.c']
 support=out/'support';support.mkdir();resource=subprocess.check_output([str(a.llvm/'clang'),'-print-resource-dir'],text=True).strip()
 support_records={}
 for source in sources:
  target=support/(source.stem+'.o');cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin',
     '-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',str(Path(resource)/'include'),'-Wall','-Wextra','-Werror',
     *[v for d in includes for v in ('-I',str(d))],'-c',source,'-o',target]
  run(cmd,source.stem+'-support');objects[target.name]=target
  support_records[source.name]={'source_sha256':sha(source),'object_sha256':sha(target),'command':list(map(str,cmd))}
 # Exact original IOAcceleratorES ABI, already retained by the baseline.
 sys.path.insert(0,str(a.base/'prelink-tool'));from native_prelink import macho,backed_offset
 b=(a.base/'kernel.macho').read_bytes();im=macho(b);bp=json.loads((a.base/'kernel.json').read_text())
 abi=json.loads((ROOT/'profiles/v91/reconstruction/tree/scripts/data/ioaccelerator-base-abi.json').read_text())
 if bp['original_executable_sha256']['com.apple.iokit.IOGPUFamily']!=abi['original_iogpu_text_sha256']:raise ValueError('Original IOGPU image differs')
 for addr,digest in abi['original_entry_sha256'].items():
  off=backed_offset(im['segments'],int(addr,16),32,len(b))
  if hashlib.sha256(b[off:off+32]).hexdigest()!=digest:raise ValueError('Original IOGPU entry differs: '+addr)
 bindings={'__ZN15IOAcceleratorESC2EPK11OSMetaClass':abi['protected_compatible_constructor_aliases'][0],
           '__ZN15IOAcceleratorESD2Ev':abi['nondeleting_destructor'],'__ZN15IOAcceleratorES10gMetaClassE':abi['metaclass']}
 assembly=out/'original-bindings.S';assembly.write_text(''.join('.globl '+k+'\n.set '+k+', '+v+'\n' for k,v in bindings.items()))
 bind_obj=out/'original-bindings.o';run([a.llvm/'clang','-target','arm64-apple-ios17.0','-c',assembly,'-o',bind_obj],'original-bindings')
 objects[bind_obj.name]=bind_obj
 # The two unprovided optional diagnostics are selected as null providers
 # in source, so a static kernel has no unreachable branch relocations to0.
 optional={'_boot_log_export_put','_native_pmap_user_mappings'}
 for obj in objects.values():
  if optional.intersection(global_symbols(obj)):raise ValueError('Unprovided diagnostic still has a symbol reference')
 optional_refs={'_boot_log_export_put':'source-selected absence; original refusal retained',
                '_native_pmap_user_mappings':'source-selected absence; observation only'}
 cmd=json.loads((a.base/'link-command.json').read_text())
 display_arg=next(x for x in cmd if x.startswith('-Wl,-force_load,') and x.endswith('/libNativeLegacyDisplay.late.a'))
 original_display=Path(display_arg.split(',',2)[2]);d=out/'display';d.mkdir()
 names=[n for n in subprocess.check_output(['ar','t',str(original_display)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
 if 'EldenDisplayGlue.o' not in names:raise ValueError('Expected display glue member absent')
 replacement=objects.pop('EldenDisplayGlue.o')
 for n in names:
  if Path(n).name!=n:raise ValueError('Unexpected archive member path')
  data=replacement.read_bytes() if n=='EldenDisplayGlue.o' else subprocess.check_output(['ar','p',str(original_display),n])
  (d/n).write_bytes(data)
 display=d/'libNativeLegacyDisplay.late.a';run(['ar','rcs',display,*names],'display-archive',d)
 gpu=out/'libEldenGpu.a';run(['ar','rcs',gpu,*objects.values()],'gpu-archive')
 old_output=cmd[cmd.index('-o')+1];cmd[cmd.index('-o')+1]=str(out/'native.macho')
 cmd[cmd.index(display_arg)]='-Wl,-force_load,'+str(display)
 # The actual normal reboot path invokes GPU drain before stopping storage.
 recovery=json.loads((a.recovery/'result.json').read_text());recovery_obj=a.recovery/'ShengUfs.o'
 if not recovery['passed'] or sha(recovery_obj)!=recovery['object_sha256']:raise ValueError('Recovery hook is unqualified')
 ufs_arg=next(x for x in cmd if x.startswith('-Wl,-force_load,') and x.endswith('/libShengUfs.a'))
 old_ufs=Path(ufs_arg.split(',',2)[2]);ud=out/'ufs';ud.mkdir()
 members=[n for n in subprocess.check_output(['ar','t',str(old_ufs)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
 if 'ShengUfs.o' not in members:raise ValueError('Expected UFS owner object missing')
 for n in members:
  if Path(n).name!=n:raise ValueError('Unexpected UFS member path')
  (ud/n).write_bytes(recovery_obj.read_bytes() if n=='ShengUfs.o' else subprocess.check_output(['ar','p',str(old_ufs),n]))
 ufs=ud/'libShengUfs.a';run(['ar','rcs',ufs,*members],'ufs-archive',ud);cmd[cmd.index(ufs_arg)]='-Wl,-force_load,'+str(ufs)
 cmd+=['-Wl,-force_load,'+str(gpu)]
 (out/'link-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
 (out/'input-audit.json').write_text(json.dumps({'gpu_objects':{k:sha(v) for k,v in objects.items()},'original_bindings':bindings,
   'support':support_records,'optional_absence':optional_refs,'display_replaced_member':'EldenDisplayGlue.o','source_built_baseline':str(a.base),'fresh_end_to_end':False},indent=2)+'\n')
 run(cmd,'native-link',out)
 if sha(a.original_kernelcache)!=bp['original_sha256']:raise ValueError('Original kernelcache differs')
 plan=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']
 cmd=[sys.executable,a.base/'prelink-tool/emit_prelink.py','--kernel',out/'native.macho','--prelinked',a.original_kernelcache,
      '--slide='+str(bp['slide']),'--output',out/'kernel.macho']
 for name in plan['roots']:cmd+=['--root',name]
 for name in plan['early_security_initializers']:cmd+=['--early-security-init',name]
 run(cmd,'prelink',out)
 count=verify_original_executables(out/'kernel.macho',json.loads((out/'kernel.json').read_text()),bp['original_executable_sha256'])
 (out/'result.json').write_text(json.dumps({'passed':True,'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),
   'original_execution_regions_preserved':count,'fresh_end_to_end':False,'boot_packaged':False,'device_access':False},indent=2)+'\n')
 print('PASS: GPU kernel linked/prelinked; final fresh build and runtime qualification still required')
if __name__=='__main__':main()
