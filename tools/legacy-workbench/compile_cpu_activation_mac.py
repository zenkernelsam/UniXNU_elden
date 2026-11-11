"""Compile the deferred CPU source without replacing the audited full kernel."""
from pathlib import Path
import hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-activation-20261111';full=r/'research/cpu-full-20261111';t=full/'tree'
source=p/'ArmCpu.deferred.cpp';manifest=json.loads((p/'source.json').read_text())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert sha(source)==manifest['deferred_sha256']
original=json.loads((t/'build/armcpu/compile-command.json').read_text())
old=str(t/'drivers/ArmCpu/ArmCpu.cpp');assert original.count(old)==1
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');report={}
for mode in ('enabled','disabled'):
    dest=p/('ArmCpu.'+mode+'.o');cmd=[str(source) if x==old else x for x in original]
    cmd[cmd.index('-o')+1]=str(dest)
    assert '-mgeneral-regs-only' in cmd
    # The original full-build environment supplies this via CCC_OVERRIDE_OPTIONS.
    cmd+=['-Wno-unknown-warning-option','-DELDEN_ARMCPU_DEFERRED='+('1' if mode=='enabled' else '0')]
    with (p/(mode+'.log')).open('w') as log:
        done=subprocess.run(cmd,cwd=record['directory'],stdout=log,stderr=subprocess.STDOUT)
    if done.returncode:raise SystemExit(mode+' native compile failed; read its log')
    assembly=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',dest],text=True)
    (p/(mode+'.asm')).write_text(assembly)
    ins=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',assembly,re.M)
    assert ins and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i) for i in ins)
    symbols=subprocess.check_output([llvm/'llvm-nm',dest],text=True)
    (p/(mode+'.nm')).write_text(symbols)
    assert '_elden_armcpu_release_secondaries' in symbols
    assert ('_elden_smp_recovery_ready' in symbols)==(mode=='enabled')
    report[mode]={'object_sha256':sha(dest),'instructions':len(ins),'fp_simd':False,'command':cmd}
assert sha(full/'out/kernel.macho')=='1858feded630624af97aa73c2d182d92832e018944d0212fb0d2fe212f96313f'
report.update({'source_sha256':sha(source),'full_kernel_unchanged':True,'boot_image_created':False,'hardware_tested':False})
(p/'native.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:({i:v for i,v in row.items() if i!='command'} if isinstance(row,dict) else row) for k,row in report.items()},indent=2))
