"""Inspect guarded-SMP native functions and actual archive membership."""
from pathlib import Path
import hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-guard-20261111';t=p/'tree';out=p/'native-audit';out.mkdir(exist_ok=True)
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');records={}
for group,obj,archive in [('armcpu',t/'build/armcpu/ArmCpu.o',t/'build/armcpu/libArmCpu.a'),('ufs',t/'build/sheng-ufs/ShengUfs.o',t/'build/sheng-ufs/libShengUfs.a')]:
    data=obj.read_bytes();member=subprocess.check_output([llvm/'llvm-ar','p',archive,obj.name]);assert member==data or member==data+b'\n'*((-len(data))%8)
    nm=subprocess.check_output([llvm/'llvm-nm','--defined-only',obj],text=True)
    names=[]
    for line in nm.splitlines():
        fields=line.split()
        if len(fields)!=3 or fields[1].lower()!='t':continue
        name=fields[2]
        if any(x in name for x in ('eldenSmp','elden_smp_recovery_ready','guardProcessor','guardIpi','releaseSecondaries','elden_armcpu_guard','elden_panic_return_to_fastboot')):names.append(name)
    assert names,group
    rows={}
    for name in names:
        asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble-symbols='+name,'--reloc',obj],text=True)
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M);assert ops,name
        fp=[op for op in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',op)]
        assert not fp,(name,fp)
        (out/(name+'.asm')).write_text(asm)
        rows[name]={'instructions':len(ops),'fp_simd':False}
    records[group]={'object_sha256':hashlib.sha256(data).hexdigest(),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'functions':rows}
assert '_elden_smp_recovery_ready' in records['ufs']['functions']
assert any('eldenSmpGuardWorker' in n for n in records['ufs']['functions'])
assert any('eldenSmpWorkThread' in n for n in records['ufs']['functions'])
assert any('guardProcessor' in n for n in records['armcpu']['functions'])
report={'accepted_native':True,'objects':records,'hardware_tested':False}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
