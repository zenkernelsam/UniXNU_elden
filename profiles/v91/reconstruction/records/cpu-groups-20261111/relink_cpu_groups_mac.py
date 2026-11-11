"""Rebuild two changed CPU8 objects and reproduce the recorded kernel link."""
from pathlib import Path
import hashlib,json,os,re,shlex,shutil,struct,subprocess,sys,zipfile,time
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
parent=r/'research/cpu-guard-20261111';tree=parent/'tree';p=r/'research/cpu-groups-20261111'
objroot=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837';name='mach.development.bcm2837';links=objroot/(name+'.link')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
with zipfile.ZipFile(p/'source.zip') as z:
 source=json.loads(z.read('source.json'))
 for n,h in source['files'].items():
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h;f=p/n;assert f.resolve().is_relative_to(p.resolve());f.write_bytes(b)
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
flags=shlex.split((objroot/'.LDFLAGS').read_text().rstrip('\0\n'));assert flags.pop()=='-lcc_kext'
archives=shlex.split((links/(name+'.linkarchives')).read_text())
extra=[str(objroot/n) for n in ('lastkerneldataconst.o','lastkernelconstructor.o','nonlto.o','version.o')]
baseline=objroot/(name+'.unstripped.noctf');expected=sha(baseline)
def link(dest,libs):
 cmd=flags+['-filelist',str(objroot/'link.filelist'),*extra,'-o',str(dest),'-lcc_kext',*libs]
 with dest.with_suffix('.log').open('w') as log:q=subprocess.run(cmd,cwd=objroot,env=env,stdout=log,stderr=subprocess.STDOUT)
 assert q.returncode==0,'See '+str(dest.with_suffix('.log'))
 return cmd
def sections(f):
 b=f.read_bytes();n,size=struct.unpack_from('<II',b,16);at=32;out={}
 for _ in range(n):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   count=struct.unpack_from('<I',b,at+64)[0]
   for j in range(count):
    x=at+72+80*j;sect=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
    addr,size,offset=struct.unpack_from('<QQI',b,x+32);kind=struct.unpack_from('<I',b,x+64)[0]
    data=b'' if kind&255 in (1,12,18) else b[offset:offset+size]
    out[seg+'/'+sect]=(addr,size,kind,hashlib.sha256(data).hexdigest())
  at+=length
 return out
control=p/'unchanged-link.macho';control_cmd=link(control,archives)
assert sections(control)==sections(baseline),'Recorded no-change link differs in runtime sections'
sys.path.insert(0,str(r/'scripts'));import make_entry_probe_kernel as maker
symbols=lambda f:{n:v for n,v in maker.symbols(f.read_bytes()).items() if n.startswith('_') and v>=0xfffffff000000000}
assert symbols(control)==symbols(baseline),'Recorded no-change link differs in defined kernel symbols'
print('Recorded link reproduced every runtime section and defined symbol.',flush=True)
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');records={};replacements={}
for group,command_file,src_name,obj_name,archive_name in (
 ('armcpu','build/armcpu/compile-command.json','ArmCpu.cpp','ArmCpu.o','libArmCpu.a'),
 ('ufs','build/sheng-ufs/compile-command.json','NativeUfs.cpp','ShengUfs.o','libShengUfs.a')):
 dest=p/group;dest.mkdir(exist_ok=True);cmd=json.loads((tree/command_file).read_text())
 old_archive=tree/('build/armcpu' if group=='armcpu' else 'build/sheng-ufs')/archive_name
 old_source=next(Path(x) for x in cmd if x.endswith('/'+src_name))
 if group=='armcpu':new_source=p/'ArmCpu.cpp'
 else:
  subprocess.run(['cp','-cR',old_source.parent,dest/'src'],check=True)
  target=dest/'src/EldenSdamRecovery.inc';assert target.resolve().is_relative_to(dest.resolve()) and not target.is_symlink();shutil.copyfile(p/'EldenSdamRecovery.inc',target)
  new_source=dest/'src'/src_name
 cleaned=[];i=0
 while i<len(cmd):
  x=cmd[i]
  if x in ('-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
  if x in ('-MD','-MMD','-MP'):i+=1;continue
  cleaned.append(str(new_source) if x==str(old_source) else x);i+=1
 cmd=cleaned;cmd[cmd.index('-o')+1]=str(dest/obj_name)
 if '-mgeneral-regs-only' not in cmd:cmd.append('-mgeneral-regs-only')
 cmd.append('-Wno-unknown-warning-option')
 record=json.loads((objroot/'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
 with (dest/'compile.log').open('w') as log:q=subprocess.run(cmd,cwd=record['directory'],env=env,stdout=log,stderr=subprocess.STDOUT)
 assert q.returncode==0,'See '+str(dest/'compile.log')
 new_archive=dest/archive_name;shutil.copyfile(old_archive,new_archive)
 subprocess.run(['/usr/bin/ar','rcs',new_archive,dest/obj_name],check=True)
 replacements[str(old_archive)]=str(new_archive)
 asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',dest/obj_name],text=True);(dest/'object.asm').write_text(asm)
 records[group]={'source':str(new_source),'source_sha256':sha(new_source),'object_sha256':sha(dest/obj_name),'archive':str(new_archive),'archive_sha256':sha(new_archive),'command':cmd}
 print(group+' rebuilt without changing the v81 tree.',flush=True)
new_archives=[]
for arg in archives:
 for old,new in replacements.items():arg=arg.replace(old,new)
 new_archives.append(arg)
assert sum(a!=b for a,b in zip(archives,new_archives))==2
native=p/'native.macho';link_cmd=link(native,new_archives)
out=r/'out/startup-basic-bundle-v82-20261108';out.mkdir(exist_ok=False)
line=next(s[2:] for s in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if s.startswith('+ ') and '/emit_prelink.py ' in s)
cmd=shlex.split(line);cmd[0]=str(r/'.venv/bin/python');cmd[1]=str(tree/'kits/XnuPrelink/emit_prelink.py');cmd[cmd.index('--kernel')+1]=str(native);cmd[cmd.index('--output')+1]=str(out/'kernel.macho')
with (p/'prelink.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
assert sha(baseline)==expected
report={'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(native),'parent_native_sha256':expected,'parent_unchanged':True,
 'control_relink_sha256':sha(control),'control_runtime_sections_and_symbols_equal':True,'objects':records,'source':source,'link_command':link_cmd,'control_link_command':control_cmd,'hardware_tested':False}
(p/'result.json').write_text(json.dumps(report,indent=2)+'\n');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'kernel_sha256':report['kernel_sha256'],'parent_unchanged':True,'zero_change_link_verified':True,'boot_image_created':False},indent=2))
