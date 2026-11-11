from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r;p=r/'research/icon-fault-trace-20261111';parent=r/'research/cpu-hold-20261111';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
with zipfile.ZipFile(p/'source.zip') as z:
 m=json.loads(z.read('source.json'))
 for n,h in m['files'].items():
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h;(p/n).write_bytes(b)
subprocess.run(['/usr/bin/clang','-std=c11','-O1','-fsanitize=address,undefined',p/'TestFaultFrames.c','-o',p/'test-frames'],check=True)
test=subprocess.run([p/'test-frames'],capture_output=True,text=True,check=True)
(p/'host.json').write_text(json.dumps({'passed':True,'stdout':test.stdout,'stderr':test.stderr,'policy_sha256':sha(p/'FaultFrames.h'),'test_sha256':sha(p/'TestFaultFrames.c')},indent=2)+'\n')
dest=p/'src'
assert not (p/'result.json').exists(),'Do not overwrite a published result'
if not dest.exists():subprocess.run(['cp','-cR',parent/'ufs/src',dest],check=True)
assert sha(dest/'NativeUfs.cpp')==sha(parent/'ufs/src/NativeUfs.cpp')
assert sha(dest/'EldenUsbDebugXnu.inc') in (m['parent_usb_sha256'],m['files']['EldenUsbDebugXnu.inc'])
for n in ('EldenUsbDebugXnu.inc','IconFaultTrace.inc','FaultFrames.h'):shutil.copyfile(p/n,dest/n)
old=json.loads((parent/'result.json').read_text());base=old['objects']['ufs']['command'];cmd=[];i=0
for i,x in enumerate(base):cmd.append(str(dest/'NativeUfs.cpp') if x.endswith('/NativeUfs.cpp') else x)
cmd[cmd.index('-o')+1]=str(p/'ShengUfs.o');assert '-mgeneral-regs-only' in cmd
record=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
directory=json.loads(record.read_text().strip().rstrip(','))['directory']
with (p/'compile.log').open('w') as log:subprocess.run(cmd,cwd=directory,env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option'),stdout=log,stderr=subprocess.STDOUT,check=True)
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',p/'ShengUfs.o'],text=True);(p/'object.asm').write_text(asm)
ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
bad=[x for x in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x)];assert ops and not bad,bad[:8]
assert 'eldenIconFaultTrace' in asm and '_elden_icon_fault_capture' in asm
recpath=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
rec=json.loads(recpath.read_text().strip().rstrip(','));args=rec['arguments'];cc=[];i=0
while i<len(args):
 x=args[i]
 if x in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
 if x in ('-MD','-MMD','-MP'):i+=1;continue
 if x=='-Xclang' and args[i+1].startswith('-W'):i+=2;continue
 if not x.startswith('-W'):cc.append(str(p/'IconFaultCapture.c') if x==rec['file'] else x)
 i+=1
cc+=['-o',str(p/'IconFaultCapture.o'),'-I',str(p),'-mgeneral-regs-only','-Wno-unknown-warning-option']
with (p/'capture-compile.log').open('w') as log:subprocess.run(cc,cwd=rec['directory'],env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option'),stdout=log,stderr=subprocess.STDOUT,check=True)
capture_asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',p/'IconFaultCapture.o'],text=True);(p/'capture.asm').write_text(capture_asm)
capture_ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',capture_asm,re.M)
assert capture_ops and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in capture_ops)
assert '_copyin' in capture_asm
archive=p/'libShengUfs.a';shutil.copyfile(parent/'ufs/libShengUfs.a',archive);subprocess.run(['/usr/bin/ar','rcs',archive,p/'ShengUfs.o',p/'IconFaultCapture.o'],check=True)
report={'host_passed':True,'source':m,'archive_sha256':sha(archive),'object_sha256':sha(p/'ShengUfs.o'),'capture_object_sha256':sha(p/'IconFaultCapture.o'),'capture_instructions':len(capture_ops),'instructions':len(ops),'fp_simd':False,'command':cmd,'capture_command':cc,'kernel_linked':False,'hardware_tested':False}
(p/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('host_passed','archive_sha256','instructions','fp_simd','kernel_linked')},indent=2))
