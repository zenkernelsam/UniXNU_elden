from pathlib import Path
import json,hashlib,shutil,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');t=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
x=t/'.cache/xnu-work';o=r/'research/cpu-affinity-20261111';o.mkdir(exist_ok=True)
files=['osfmk/arm64/start.s','osfmk/arm64/machine_routines.c','pexpert/pexpert/arm64/board_config.h','pexpert/pexpert/arm64/BCM2837.h','osfmk/arm64/proc_reg.h','osfmk/arm64/cpu_data_internal.h','osfmk/arm64/cpu_data.c','osfmk/kern/sched_clutch.c']
manifest={}
for n in files:
 p=x/n
 if p.exists():
  dest=o/'baseline'/n;dest.parent.mkdir(parents=True,exist_ok=True);b=p.read_bytes();dest.write_bytes(b);manifest[n]=hashlib.sha256(b).hexdigest()
(o/'manifest.json').write_text(json.dumps(manifest,indent=2))
result=subprocess.run(['git','-C',str(x),'grep','-n','-E','cluster_offsets|MPIDR_AFF[012]_MASK|MPIDR_AFF[012]_SHIFT','--','osfmk/arm64','osfmk/arm','pexpert/arm','pexpert/arm64'],capture_output=True)
(o/'consumers.txt').write_bytes(result.stdout)
cc=t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/compile_commands.json'
commands=json.loads(cc.read_text());chosen=[c for c in commands if c.get('file','').endswith(('/start.s','/machine_routines.c'))]
(o/'compile-commands.json').write_text(json.dumps(chosen,indent=2))
print(json.dumps({'files':len(manifest),'compile_commands':len(chosen),'matches':len(result.stdout.splitlines())}))
