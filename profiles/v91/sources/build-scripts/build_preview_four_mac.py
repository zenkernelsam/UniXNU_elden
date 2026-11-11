"""Reproduce the frozen display object, then compile only its bounded preview delta."""
from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/preview-four-20261111';tree=r/'research/cpu-guard-20261111/tree';d=tree/'build/native-legacy-display'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
original=tree/'scripts/driver-patches/EldenDisplayGlue.cpp';new=p/'EldenDisplayGlue.cpp'
expected=original.read_text().replace('extern "C" void elden_preview_after_input(void){__atomic_store_n(&previewAfterInput,2,__ATOMIC_RELEASE);}',
 'extern "C" void elden_preview_after_input(void){__atomic_store_n(&previewAfterInput,4,__ATOMIC_RELEASE);}')
assert new.read_text()==expected
def sections(f):
 b=f.read_bytes();n=struct.unpack_from('<I',b,16)[0];at=32;out={}
 for _ in range(n):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   count=struct.unpack_from('<I',b,at+64)[0]
   for j in range(count):
    x=at+72+80*j;sect=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
    size,offset=struct.unpack_from('<QI',b,x+40);kind=struct.unpack_from('<I',b,x+64)[0]
    if seg=='__DWARF':continue
    data=b'' if kind&255 in (1,12,18) else b[offset:offset+size]
    out[seg+'/'+sect]={'size':size,'flags':kind,'sha256':hashlib.sha256(data).hexdigest()}
  at+=length
 return out
base=json.loads((tree/'build/sheng-ufs/compile-command.json').read_text());source=next(x for x in base if x.endswith('/NativeUfs.cpp'))
features=json.loads((d/'build-report.json').read_text())['selected_owned_features']
record=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
def compile(src,dest):
 cmd=[];i=0
 while i<len(base):
  x=base[i]
  if x in ('-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
  if x in ('-MD','-MMD','-MP'):i+=1;continue
  cmd.append(str(src) if x==source else x);i+=1
 cmd[cmd.index('-o')+1]=str(dest);cmd+=['-I',str(d),*['-D'+x+'=1' for x in features],'-Wno-unknown-warning-option']
 with dest.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=record['directory'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
control=p/'control.o';control_command=compile(original,control)
before=sections(d/'EldenDisplayGlue.o');same=sections(control)
(p/'control-sections.json').write_text(json.dumps({'frozen':before,'rebuilt':same},indent=2)+'\n')
assert before==same,'Control compile differs: inspect control-sections.json; do not replace frozen archive'
obj=p/'EldenDisplayGlue.o';command=compile(new,obj)
after=sections(obj);changed=[name for name in before if before[name]!=after[name]]
assert changed==['__TEXT/__text'],changed
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
for name,f in [('before',control),('after',obj)]:
 asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',f],text=True)
 (p/(name+'.asm')).write_text(asm)
archive=p/'libNativeLegacyDisplay.late.a';shutil.copyfile(d/archive.name,archive)
subprocess.run(['/usr/bin/ar','rcs',archive,obj],check=True)
member=subprocess.check_output([llvm/'llvm-ar','p',archive,obj.name]);assert member==obj.read_bytes() or member==obj.read_bytes()+b'\n'*((-obj.stat().st_size)%8)
report={'parent_archive_sha256':sha(d/archive.name),'archive_sha256':sha(archive),'object_sha256':sha(obj),'source_sha256':sha(new),'parent_source_sha256':sha(original),'control_sections_equal':True,'changed_sections':changed,'command':command,'control_command':control_command,'hardware_tested':False,'kernel_linked':False}
(p/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
