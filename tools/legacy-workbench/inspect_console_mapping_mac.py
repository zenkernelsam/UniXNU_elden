"""Read-only preprocessing of the exact frozen CPU8 console compilation."""
from pathlib import Path
import hashlib,json,os,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/display-alias-review-20261111';p.mkdir(exist_ok=True)
record_path=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/video_console.o.json'
record=json.loads(record_path.read_text().strip().rstrip(','));args=record['arguments'];cmd=[];i=0
while i<len(args):
 x=args[i]
 if x in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=2;continue
 if x in ('-c','-MD','-MMD','-MP'):i+=1;continue
 if x=='-Xclang' and args[i+1].startswith('-W'):i+=2;continue
 if not x.startswith('-W'):cmd.append(x)
 i+=1
cmd+=['-E','-dM','-Wno-unknown-warning-option']
q=subprocess.run(cmd,cwd=record['directory'],capture_output=True,text=True,env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option'),check=True)
(p/'macros.txt').write_text(q.stdout)
names=('HAS_UCNORMAL_MEM','VM_WIMG_IO','VM_WIMG_WCOMB','VM_WIMG_RT','MAX_CPUS','MAX_CPU_CLUSTERS')
values={}
for line in q.stdout.splitlines():
 parts=line.split(None,2)
 if len(parts)==3 and parts[1] in names:values[parts[1]]=parts[2]
report={'macros':values,'record_sha256':hashlib.sha256(record_path.read_bytes()).hexdigest(),'source':record['file'],'source_sha256':hashlib.sha256(Path(record['file']).read_bytes()).hexdigest(),'command':cmd,'device_accessed':False,'mapping_changed':False}
(p/'macros.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(values))
