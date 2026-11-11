"""Compile isolated topology/reset objects. Never link or alter a boot candidate."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-affinity-20261111';candidate=p/'candidate'
manifest=json.loads((p/'candidate-manifest.json').read_text())
for n,h in manifest['files'].items():assert hashlib.sha256((candidate/n).read_bytes()).hexdigest()==h
commands=json.loads((p/'compile-commands.json').read_text());out=p/'native';out.mkdir(exist_ok=True)
strip_pair={'-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'}
strip_single={'-c','-E','-MD','-MMD','-MP','-MG','-save-temps','-save-temps=obj','-ftime-trace'}
def clean(row):
    old=row['arguments'];result=[];i=0
    while i<len(old):
        a=old[i]
        if a in strip_pair:i+=2;continue
        if a in strip_single:i+=1;continue
        assert not a.startswith(('-save-temps=','-ftime-trace=')),a
        result.append(a);i+=1
    return result
record={'linked':False,'boot_candidate':False,'hardware_tested':False,'objects':{},'macros':{}}
for row in commands:
    name=Path(row['file']).name
    for variant in ('baseline','candidate'):
        args=clean(row)
        if variant=='candidate':
            relative=Path(row['file']).relative_to(Path(row['file'].split('/.cache/xnu-work/')[0])/'.cache/xnu-work')
            args=[str(candidate/relative) if x==row['file'] else x for x in args]
            args[1:1]=['-I',str(candidate/'pexpert')]
        macros=subprocess.run(args+['-E','-dM'],cwd=row['directory'],capture_output=True,check=True)
        (out/(variant+'-'+name+'.macros')).write_bytes(macros.stdout)
        interesting={}
        for line in macros.stdout.decode().splitlines():
            words=line.split(maxsplit=2)
            if len(words)==3 and words[1] in ('MAX_CPUS','MAX_CPU_CLUSTERS','MAX_CPU_CLUSTER_PHY_ID','HAS_CLUSTER','NO_CPU_OVRD','CONFIG_KERNEL_INTEGRITY','CONFIG_SPTM','XNU_NATIVE_ARCH_TIMER','__ARM_KERNEL_PROTECT__','XNU_EARLY_BOOT_PROGRESS'):
                interesting[words[1]]=words[2]
        obj=out/(variant+'-'+name+'.o')
        with (out/(variant+'-'+name+'.log')).open('wb') as log:
            subprocess.run(args+['-c','-o',str(obj)],cwd=row['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
        dis=subprocess.check_output(['/Users/ciscohe/toolchains/llvm-20/bin/llvm-objdump','--disassemble','--reloc',str(obj)])
        (out/(variant+'-'+name+'.asm')).write_bytes(dis)
        key=variant+'/'+name
        record['objects'][key]={'sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'bytes':obj.stat().st_size}
        record['macros'][key]=interesting
        print(key,interesting,flush=True)
for name in ('start.s','machine_routines.c'):
    assert record['macros']['candidate/'+name]['MAX_CPUS']=='8'
    assert record['macros']['candidate/'+name]['MAX_CPU_CLUSTERS']=='2'
    assert record['macros']['candidate/'+name]['HAS_CLUSTER']=='1'
    assert record['macros']['candidate/'+name]['NO_CPU_OVRD']=='1'
(out/'report.json').write_text(json.dumps(record,indent=2)+'\n')
with zipfile.ZipFile(p/'native-results.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in out.iterdir():
        if f.is_file():z.write(f,f.name)
print('Object checks complete; no kernel link or device action.',flush=True)
