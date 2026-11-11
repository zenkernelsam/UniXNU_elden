"""Build guarded CPU8 in a new private clone. No signing or device operation."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,shlex,zipfile,time
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
old=r/'research/cpu-full-20261111';p=r/'research/cpu-guard-20261111';tree=p/'tree';assert not tree.exists()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert sha(old/'out/kernel.macho')=='1858feded630624af97aa73c2d182d92832e018944d0212fb0d2fe212f96313f'
with zipfile.ZipFile(p/'source.zip') as z:
 manifest=json.loads(z.read('source.json'))
 for n,h in manifest['files'].items():
  data=z.read(n);assert hashlib.sha256(data).hexdigest()==h
  target=p/n;assert target.resolve().is_relative_to(p.resolve());target.write_bytes(data)
subprocess.run(['cp','-cR',old/'tree',tree],check=True)
subprocess.run(['cp','-cR',old/'archives',p/'archives'],check=True)
x=tree/'.cache/xnu-work'
top=Path(subprocess.check_output(['git','-C',x,'rev-parse','--show-toplevel'],text=True).strip())
common=subprocess.check_output(['git','-C',x,'rev-parse','--git-common-dir'],text=True).strip()
assert top.resolve()==x.resolve() and (x/common).resolve().is_relative_to(x.resolve())
for relative,source in [('drivers/ArmCpu/ArmCpu.cpp','ArmCpu.cpp'),('scripts/driver-patches/EldenSdamRecovery.inc','EldenSdamRecovery.inc')]:
 target=tree/relative;assert target.resolve().is_relative_to(tree.resolve()) and not target.is_symlink()
 shutil.copyfile(p/source,target)
# Remap copied compile records before native builders consume them. Headers and
# the other objects already come from the same audited CPU8 configuration.
for f in (tree/'build').rglob('*.json'):
 if f.stat().st_size>20_000_000:continue
 text=f.read_text();changed=text.replace(str(old),str(p))
 if changed!=text:f.write_text(changed)
env=dict(os.environ,**json.loads((old/'final-environment.json').read_text()))
for k,v in list(env.items()):
 if k.startswith(('XNU_','ELDEN_','CONFIG_')):env[k]=v.replace(str(old),str(p))
env['XNU_BOOTSTRAP']='0';env['JOBS']='4'
record=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
cmd=json.loads((tree/'build/armcpu/compile-command.json').read_text());cmd+=['-DELDEN_ARMCPU_DEFERRED=1']
assert '-mgeneral-regs-only' in cmd
with (p/'armcpu-build.log').open('w') as log:result=subprocess.run(cmd,cwd=record['directory'],env=env,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'ArmCpu failed; inspect armcpu-build.log'
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');obj=tree/'build/armcpu/ArmCpu.o'
asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',obj],text=True)
ins=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
assert ins and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',s) for s in ins)
(p/'ArmCpu.asm').write_text(asm)
subprocess.run(['/usr/bin/ar','rcs',tree/'build/armcpu/libArmCpu.a',obj],check=True)
(tree/'build/armcpu/compile-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
print('Deferred ArmCpu compiled; rebuilding UFS recovery integration.',flush=True)
start=time.monotonic()
with (p/'ufs-build.log').open('w') as log:result=subprocess.run([r/'.venv/bin/python',tree/'scripts/build-sheng-ufs-probe.py'],cwd=tree,env=env,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'UFS guard integration failed; inspect ufs-build.log'
print('Guard integration built; linking CPU8 kernel.',flush=True)
with (p/'link.log').open('w') as log:result=subprocess.run(['bash',tree/'scripts/build-xnu-4k-baseline.sh'],cwd=tree,env=env,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'CPU guard link failed; inspect link.log'
kernel=tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf'
out=r/'out/startup-cpu-guard-v81-20261111';out.mkdir(exist_ok=False)
line=next(s[2:] for s in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if s.startswith('+ ') and '/emit_prelink.py ' in s)
cmd=shlex.split(line);cmd[0]=str(r/'.venv/bin/python');cmd[1]=str(tree/'kits/XnuPrelink/emit_prelink.py')
cmd[cmd.index('--kernel')+1]=str(kernel);cmd[cmd.index('--output')+1]=str(out/'kernel.macho')
with (p/'prelink.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
assert sha(old/'out/kernel.macho')=='1858feded630624af97aa73c2d182d92832e018944d0212fb0d2fe212f96313f'
report={'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(kernel),'armcpu_object_sha256':sha(obj),'armcpu_no_fp':True,
 'armcpu_instructions':len(ins),'seconds':time.monotonic()-start,'baseline_kernel_unchanged':True,'boot_image_created':False,'hardware_tested':False,'source':manifest}
(p/'result.json').write_text(json.dumps(report,indent=2)+'\n');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
selected={k:v for k,v in env.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}
(p/'environment.json').write_text(json.dumps(selected,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='source'},indent=2),flush=True)
