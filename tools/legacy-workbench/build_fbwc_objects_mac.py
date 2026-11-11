"""Build the isolated framebuffer mapping candidate, without device operations."""
from pathlib import Path
import hashlib,json,os,re,shutil,struct,subprocess,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r;p=r/'research/fbwc-20261111';tree=r/'research/cpu-guard-20261111/tree';objroot=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837';display=tree/'build/native-legacy-display';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert not (p/'objects.json').exists()
with zipfile.ZipFile(p/'source.zip') as z:
 source=json.loads(z.read('source.json'))
 for n,h in source['files'].items():
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h;(p/n).write_bytes(b)
subprocess.run(['/usr/bin/clang','-std=c11','-O1','-fsanitize=address,undefined',p/'TestWcPolicy.c','-o',p/'test-policy'],check=True)
test=subprocess.run([p/'test-policy'],capture_output=True,text=True,check=True)
(p/'host.json').write_text(json.dumps({'passed':True,'stdout':test.stdout,'stderr':test.stderr,'policy_sha256':sha(p/'WcPolicy.h'),'test_sha256':sha(p/'TestWcPolicy.c')},indent=2)+'\n')
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');records={}
def compile(src,out,base,oldsource,directory,extra=()):
 cmd=[];i=0
 while i<len(base):
  x=base[i]
  if x in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
  if x in ('-MD','-MMD','-MP'):i+=1;continue
  if x=='-Xclang' and base[i+1].startswith('-W'):i+=2;continue
  if not x.startswith('-W'):cmd.append(str(src) if x==str(oldsource) else x)
  i+=1
 cmd+=['-o',str(out),'-I',str(p),'-I',str(Path(oldsource).parent),'-Wno-unknown-warning-option',*extra]
 with out.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=directory,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',out],text=True);out.with_suffix('.asm').write_text(asm)
 records[out.name]={'source_sha256':sha(src),'object_sha256':sha(out),'command':cmd}
 return asm
def sections(path):
 b=path.read_bytes();at=32;result={}
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for j in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+j*80;name=b[x:x+16].split(b'\0')[0];seg=b[x+16:x+32].split(b'\0')[0]
    if seg==b'__DWARF':continue
    address,size,off=struct.unpack_from('<QQI',b,x+32);flags=struct.unpack_from('<I',b,x+64)[0]
    result[(seg,name)]=(address,size,flags,b'' if flags&255 in (1,12,18) else b[off:off+size])
  at+=length
 return result
c=json.loads((objroot/'osfmk/DEVELOPMENT/video_console.o.json').read_text().strip().rstrip(','));assert sha(Path(c['file']))==source['parent_console_sha256']
compile(Path(c['file']),p/'video_console-control.o',c['arguments'],c['file'],c['directory'])
assert sections(p/'video_console-control.o')==sections(objroot/'osfmk/DEVELOPMENT/video_console.o')
compile(p/'video_console.c',p/'video_console.o',c['arguments'],c['file'],c['directory'])
c=json.loads((objroot/'osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
mapasm=compile(p/'MappingCheck.c',p/'MappingCheck.o',c['arguments'],c['file'],c['directory'],['-mgeneral-regs-only'])
u=json.loads((r/'research/icon-fault-trace-20261111/result.json').read_text());srcdir=p/'ufs-src'
if not srcdir.exists():subprocess.run(['cp','-cR',r/'research/icon-fault-trace-20261111/src',srcdir],check=True)
shutil.copyfile(p/'EldenSdamRecovery.inc',srcdir/'EldenSdamRecovery.inc')
ior=json.loads((objroot/'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','));base=u['command'];oldsrc=next(x for x in base if x.endswith('/NativeUfs.cpp'))
compile(srcdir/'NativeUfs.cpp',p/'ShengUfs.o',base,oldsrc,ior['directory'])
features=json.loads((display/'build-report.json').read_text())['selected_owned_features']
glue=compile(p/'EldenDisplayGlue.cpp',p/'EldenDisplayGlue.o',base,oldsrc,ior['directory'],['-I',str(display),*['-D'+x+'=1' for x in features]])
native=compile(p/'NativeLegacyDisplay.cpp',p/'NativeLegacyDisplay.o',base,oldsrc,ior['directory'],['-I',str(display),'-I',str(display/'src')])
subprocess.run([llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeLegacyDisplay.cpp',p/'NativeLegacyDisplay.o',p/'NativeLegacyDisplay.late.o'],check=True)
for name in ('MappingCheck','ShengUfs','EldenDisplayGlue','NativeLegacyDisplay'):
 text=(p/(name+'.asm')).read_text();ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M)
 bad=[x for x in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x)];assert ops and not bad,(name,bad[:6])
 records[name+'.o']['instructions']=len(ops);records[name+'.o']['fp_simd']=False
assert 'at\ts1e1r' in mapasm or 'at\ts1e1r,' in mapasm or 'at\ts1e1r' in mapasm.replace(' ','')
assert 'par_el1' in mapasm.lower() and 'mair_el1' in mapasm.lower()
ua=p/'libShengUfs.a';shutil.copyfile(r/'research/icon-fault-trace-20261111/libShengUfs.a',ua);subprocess.run(['/usr/bin/ar','rcs',ua,p/'ShengUfs.o'],check=True)
da=p/'libNativeLegacyDisplay.late.a';shutil.copyfile(r/'research/preview-four-20261111/libNativeLegacyDisplay.late.a',da);subprocess.run(['/usr/bin/ar','rcs',da,p/'EldenDisplayGlue.o',p/'NativeLegacyDisplay.late.o',p/'MappingCheck.o'],check=True)
report={'host_passed':True,'source':source,'objects':records,'ufs_archive_sha256':sha(ua),'display_archive_sha256':sha(da),'kernel_linked':False,'hardware_tested':False}
(p/'objects.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'objects':list(records),'host_passed':True,'kernel_linked':False},indent=2))
