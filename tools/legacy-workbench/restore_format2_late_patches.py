from pathlib import Path
import subprocess,json,hashlib
r=Path.cwd();t=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree';log=(r/'out/ipados-console-20261107/build.log').read_text();events=[]
for name in ('zzy-native-gpu-addon-mount.patch','zzy-staged-system-apps.patch','zzz-boot-progress.patch'):
 p=t/'patches/xnu'/name
 if '== kernel: patch '+name not in log:
  events.append([name,'not in original format2 stack']);continue
 cmd=['git','-C',str(t/'.cache/xnu-work'),'apply']
 a=subprocess.run(cmd+['--check',str(p)],capture_output=True)
 if a.returncode==0:
  subprocess.run(cmd+[str(p)],check=True);events.append([name,'restored known reversed layer'])
 else:
  b=subprocess.run(cmd+['--reverse','--check',str(p)],capture_output=True)
  if b.returncode:raise RuntimeError(name+' neither forward nor reverse: '+a.stderr.decode())
  events.append([name,'already applied'])
expected=json.loads((r/'out/ipados-console-20261107/panic-dispatch-test.json').read_text())['source_sha256']
actual=hashlib.sha256((t/'.cache/xnu-work/osfmk/kern/printf.c').read_bytes()).hexdigest();assert actual==expected
(r/'out/pinned-root-20261107/source-restoration.json').write_text(json.dumps(dict(events=events,printf_source_matches=True),indent=2)+'\n');print(events)
