"""Final CPU8 link/prelink in the private tree. Produces no boot image or flash."""
from pathlib import Path
import hashlib,json,os,re,shlex,shutil,subprocess,time
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-full-20261111';tree=p/'tree';out=p/'out';out.mkdir(exist_ok=True)
env=dict(os.environ,**json.loads((p/'final-environment.json').read_text()));env['XNU_BOOTSTRAP']='0'
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');cpu=tree/'build/armcpu'
command=json.loads((cpu/'compile-command.json').read_text())
if not (cpu/'ArmCpu.before-scalar.o').exists():shutil.copyfile(cpu/'ArmCpu.o',cpu/'ArmCpu.before-scalar.o')
clean=[];i=0
while i<len(command):
    if command[i] in ('-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
    if command[i] in ('-MD','-MMD','-MP'):i+=1;continue
    clean.append(command[i]);i+=1
if '-mgeneral-regs-only' not in clean:clean+=['-mgeneral-regs-only']
record=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
assert Path(record['file']).name=='IOPlatformExpert.cpp','Native builders must preserve the kernel compile record'
with (p/'armcpu-scalar.log').open('w') as log:subprocess.run(clean,cwd=record['directory'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
dis=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',cpu/'ArmCpu.o']).decode()
instructions=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',dis,re.M)
assert instructions and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',line) for line in instructions)
(p/'ArmCpu-scalar.asm').write_text(dis)
subprocess.run(['/usr/bin/ar','rcs',cpu/'libArmCpu.a',cpu/'ArmCpu.o'],check=True)
(cpu/'compile-command.json').write_text(json.dumps(clean,indent=2)+'\n')
print('ArmCpu rebuilt with general registers only; final XNU link starting.',flush=True)
start=time.monotonic()
with (p/'final-link.log').open('w') as log:
    subprocess.run(['bash',tree/'scripts/build-xnu-4k-baseline.sh'],cwd=tree,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
kernel=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf'
assert kernel.exists()
line=next(x[2:] for x in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if x.startswith('+ ') and '/emit_prelink.py ' in x)
c=shlex.split(line);c[0]=str(r/'.venv/bin/python');c[c.index('--kernel')+1]=str(kernel);c[c.index('--output')+1]=str(out/'kernel.macho')
# Read the same prelink implementation from the isolated tree.
c[1]=str(tree/'kits/XnuPrelink/emit_prelink.py')
with (p/'prelink.log').open('w') as log:subprocess.run(c,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
report={'kernel_sha256':hashlib.sha256((out/'kernel.macho').read_bytes()).hexdigest(),
 'source_kernel_sha256':hashlib.sha256(kernel.read_bytes()).hexdigest(),
 'armcpu_object_sha256':hashlib.sha256((cpu/'ArmCpu.o').read_bytes()).hexdigest(),
 'armcpu_instructions':len(instructions),'armcpu_fp_simd':False,'link_seconds':time.monotonic()-start,
 'full_link':True,'boot_image_created':False,'hardware_tested':False}
(p/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
