"""Compile the fixed bridge using the recorded kernel C recipe, without linking."""
from pathlib import Path
import hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/bt-completion-20261111';t=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
manifest=json.loads((p/'manifest.json').read_text());report={'hardware_tested':False,'linked':False,'objects':{}}
names={}
for mode,key in [('before','before'),('fixed','fixed')]:
    source=p/mode/'BtBridge.c';assert hashlib.sha256(source.read_bytes()).hexdigest()==manifest[key]
    obj=p/(mode+'.o');args=record['arguments'];command=[];i=0
    while i<len(args):
        a=args[i]
        if a==record['file']:command.append(str(source))
        elif a in ('-o','-MF','-MT','-MJ','-MQ','-serialize-diagnostics'):
            if a=='-o':command+=['-o',str(obj)]
            i+=1
        elif a in ('-MMD','-MD','-MP'):pass
        elif a=='-Xclang' and args[i+1].startswith('-W'):i+=1
        elif not a.startswith('-W'):command.append(a)
        i+=1
    command+=['-Wall','-Wextra','-Werror','-mgeneral-regs-only','-I',str(t/'.cache/xnu-work/osfmk'),'-I',str(p/mode)]
    with (p/(mode+'-native.log')).open('wb') as log:
        subprocess.run(command,cwd=record['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
    dis=subprocess.check_output(['/Users/ciscohe/toolchains/llvm-20/bin/llvm-objdump','--disassemble','--reloc',str(obj)]).decode()
    instructions=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',dis,re.M);assert instructions
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in instructions)
    (p/(mode+'.asm')).write_text(dis)
    symbols=subprocess.check_output(['/Users/ciscohe/toolchains/llvm-20/bin/llvm-nm','-g','--defined-only',str(obj)]).decode()
    names[mode]=sorted(line.split()[-1] for line in symbols.splitlines() if line.strip())
    report['objects'][mode]={'sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'source_sha256':manifest[key],'instructions':len(instructions),'fp_simd':False,'exports':names[mode]}
assert names['before']==names['fixed']
report['exports_unchanged']=True
(p/'native-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
