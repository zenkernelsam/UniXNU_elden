"""Compile the new owned library payload/telemetry and relink the frozen v89."""
from pathlib import Path
import argparse,hashlib,io,json,os,shlex,shutil,struct,subprocess,sys,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[90,91],default=90);v=a.parse_args().version
p=r/'research/graphic-variant-cpu-20261111'/('kernel-build' if v==90 else f'kernel-build-v{v}');p.mkdir(exist_ok=True)
out=r/f'out/startup-basic-bundle-v{v}-20261108';out.mkdir(exist_ok=True);assert not (out/'artifacts.zip').exists()
oldout=r/f'out/startup-basic-bundle-v{v-1}-20261108';old=json.loads((oldout/'result.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
tree=r/'research/cpu-guard-20261111/tree';objroot=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
record=json.loads((objroot/'bsd/DEVELOPMENT/kern_exec.o.json').read_text().strip().rstrip(','));original=Path(record['file'])
with zipfile.ZipFile(p/'source.zip') as z:
 source=json.loads(z.read('source.json'));assert sha(original.read_bytes())==source['parent_exec_sha256']
 dest=p/'src'
 if not dest.exists():subprocess.run(['cp','-cR',original.parent,dest],check=True)
 assert sha((dest/'kern_exec.c').read_bytes())==source['parent_exec_sha256']
 for n,h in source['files'].items():
  data=z.read(n);assert sha(data)==h;(dest/n).write_bytes(data)
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
def compile(src,obj):
 cmd=[];args=record['arguments'];i=0
 while i<len(args):
  v=args[i]
  if v in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
  if v in ('-MD','-MMD','-MP'):i+=1;continue
  if v=='-Xclang' and args[i+1].startswith('-W'):i+=2;continue
  if not v.startswith('-W'):cmd.append(str(src) if v==str(original) else v)
  i+=1
 cmd+=['-o',str(obj),'-Wno-unknown-warning-option']
 with obj.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=record['directory'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
def sections(path):
 b=path.read_bytes();at=32;result={}
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for j in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+80*j;n=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
    if seg=='__DWARF':continue
    address,size,off=struct.unpack_from('<QQI',b,x+32);flags=struct.unpack_from('<I',b,x+64)[0]
    result[seg+'/'+n]=(address,size,flags,b'' if flags&255 in (1,12,18) else b[off:off+size])
  at+=length
 return result
control=p/'control-kern_exec.o';compile(original,control)
assert sections(control)==sections(objroot/'bsd/DEVELOPMENT/kern_exec.o'),'Original compile reproduction failed'
newobj=p/'kern_exec.o';compile_command=compile(dest/'kern_exec.c',newobj)
filelist=Path(old['link_command'][old['link_command'].index('-filelist')+1]);lines=filelist.read_text().splitlines()
hits=[i for i,n in enumerate(lines) if n.endswith('/kern_exec.o') or n=='kern_exec.o'];assert len(hits)==1
lines[hits[0]]=str(newobj);newlist=p/'link.filelist';newlist.write_text('\n'.join(lines)+'\n')
def link(native,changed):
 cmd=list(old['link_command']);cmd[cmd.index('-o')+1]=str(native)
 if changed:cmd[cmd.index('-filelist')+1]=str(newlist)
 with native.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=objroot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
control_native=p/'v89-control.macho';link(control_native,False)
parent=(r/'research/icon-fault-trace-20261111/native-v89.macho' if v==90 else r/'research/graphic-variant-cpu-20261111/kernel-build/native-v90.macho');assert sha(parent.read_bytes())==old['native_sha256'] and sections(control_native)==sections(parent)
native=p/f'native-v{v}.macho';link_command=link(native,True)
line=next(s[2:] for s in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if s.startswith('+ ') and '/emit_prelink.py ' in s)
cmd=shlex.split(line);cmd[0]=sys.executable;cmd[1]=str(tree/'kits/XnuPrelink/emit_prelink.py');cmd[cmd.index('--kernel')+1]=str(native);cmd[cmd.index('--output')+1]=str(out/'kernel.macho')
with (p/'prelink.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
glyph={'source':source,'compile_command':compile_command,'object_sha256':sha(newobj.read_bytes()),'original_object_reproduced':True,'control_link_reproduced':True,'parent_kernel_sha256':old['kernel_sha256']}
build=dict(old,kernel_sha256=sha((out/'kernel.macho').read_bytes()),native_sha256=sha(native.read_bytes()),link_command=link_command,glyph_delta=glyph)
(out/'result.json').write_text(json.dumps(build,indent=2)+'\n')
subprocess.run([sys.executable,r/'out/prepare_battery_telemetry_derivative.py','--attempt','3','--build-dir',out],check=True)
with zipfile.ZipFile(oldout/'artifacts.zip') as z:files={n:z.read(n) for n in z.namelist() if n!='artifact-hashes.json'}
for n in ('kernel.json','result.json','derivative.json'):files[n]=(out/n).read_bytes()
files['kernel.macho']=(out/'entry/probe/kernel.macho').read_bytes();files['cpu-irq-snapshot-result.json']=files['result.json']
with zipfile.ZipFile(io.BytesIO(files['compiled-source.zip'])) as z:
 sources={n:z.read(n) for n in z.namelist() if n!='manifest.json'};manifest=json.loads(z.read('manifest.json'))
for n in source['files']:sources['xnu/'+n]=(dest/n).read_bytes()
libsrc=p.parent/('library-input' if v==90 else f'library-input-v{v}')
for n in ('EldenIconCpuMode.c','EldenIconCpuPaths.h','EldenGraphicVariantHook.c','EldenGraphicVariantCpu.c','EldenGraphicVariantCpu.h'):sources['package/'+n]=(libsrc/n).read_bytes()
sources['build-scripts/relink_graphic_variant_mac.py']=(p/'relink_graphic_variant_mac.py').read_bytes()
sources['build-scripts/build_graphic_variant_library_mac.py']=(p.parent/'build_graphic_variant_library_mac.py').read_bytes()
manifest['files']={n:sha(b) for n,b in sources.items()};buffer=io.BytesIO()
with zipfile.ZipFile(buffer,'w',zipfile.ZIP_DEFLATED) as z:
 for n,b in sources.items():z.writestr(n,b)
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
files['compiled-source.zip']=buffer.getvalue();files['glyph-kern_exec.o']=newobj.read_bytes();files['glyph-delta.json']=(json.dumps(glyph,indent=2)+'\n').encode()
records={'files':{n:sha(b) for n,b in files.items()},'source_files':len(sources),'hardware_tested':False}
with zipfile.ZipFile(out/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('artifact-hashes.json',json.dumps(records,indent=2)+'\n')
print(json.dumps({'kernel_sha256':sha(files['kernel.macho']),'zip_sha256':sha((out/'artifacts.zip').read_bytes()),'source_files':len(sources)},indent=2))
