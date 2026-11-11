from pathlib import Path
import json,subprocess,hashlib,re
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-irq-snapshot-20261111/mmio-fix';tree=r/'research/cpu-guard-20261111/tree'
source=p/'ArmCpu.cpp';cmd=json.loads((tree/'build/armcpu/compile-command.json').read_text());old=str(tree/'drivers/ArmCpu/ArmCpu.cpp');assert cmd.count(old)==1
cmd=[str(source) if x==old else x for x in cmd];cmd[cmd.index('-o')+1]=str(p/'ArmCpu.o');cmd+=['-Wno-unknown-warning-option']
record=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
with (p/'compile.log').open('w') as log:q=subprocess.run(cmd,cwd=record['directory'],stdout=log,stderr=subprocess.STDOUT)
assert q.returncode==0,'read compile.log'
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble-symbols=_armcpu_gic_diag','--reloc',p/'ArmCpu.o'],text=True)
(p/'gic-diag.asm').write_text(asm)
ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M);assert ops
plain=[s for s in ops if re.match(r'ldr\s+w\d+,\s*\[x\d+\]$',s)]
bad=[s for s in ops if re.match(r'(?:ldr|ldp|ldur)\b',s) and '[sp' not in s and (']!' in s or '],' in s or s.startswith('ldur'))]
assert len(plain)>=7 and not bad,(plain,bad)
assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',s) for s in ops)
result={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'object_sha256':hashlib.sha256((p/'ArmCpu.o').read_bytes()).hexdigest(),'plain_32bit_mmio_loads':len(plain),'unsafe_addressing':bad,'instructions':len(ops),'fp_simd':False,'hardware_tested':False,'full_kernel_linked':False}
(p/'native.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
