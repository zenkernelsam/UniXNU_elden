"""Compile the virtual watchdog protocol and SMCCC wrapper with CPU8 XNU ABI."""
from pathlib import Path
import json,subprocess,hashlib,re
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-activation-20261111/vdog';t=r/'research/cpu-full-20261111/tree'
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');rows={}
for name in ('GunyahWatchdog','GunyahSmc'):
    src=p/(name+'.c');obj=p/(name+'.o');args=record['arguments'];cmd=[];i=0
    while i<len(args):
        a=args[i]
        if a==record['file']:cmd.append(str(src))
        elif a in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=1
        elif a not in ('-MD','-MMD','-MP'):cmd.append(a)
        i+=1
    cmd+=['-I',str(p),'-o',str(obj),'-mgeneral-regs-only','-Wno-unknown-warning-option']
    q=subprocess.run(cmd,cwd=record['directory'],capture_output=True,text=True);(p/(name+'.log')).write_text(q.stdout+q.stderr)
    assert q.returncode==0,(name,q.stderr)
    asm=subprocess.check_output([llvm/'llvm-objdump','--disassemble','--reloc',obj],text=True);(p/(name+'.asm')).write_text(asm)
    ins=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
    assert ins and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',line) for line in ins)
    if name=='GunyahSmc':assert len([x for x in ins if re.match(r'smc\s+#?0',x)])==1
    rows[name]={'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'instructions':len(ins),'fp_simd':False,'command':cmd}
(p/'native.json').write_text(json.dumps({'objects':rows,'linked':False,'hardware_tested':False},indent=2)+'\n')
print(json.dumps({n:{k:v for k,v in row.items() if k!='command'} for n,row in rows.items()},indent=2))
