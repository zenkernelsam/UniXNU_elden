from pathlib import Path
import subprocess,json,hashlib
r=Path('/mnt/d/Desktop/Git/Patch/Y700-Elden');p=r/'research/cpu-activation-20261111'
d=Path('/mnt/d/Desktop/Git/UniXNU_elden/drivers/EldenCpuActivation')
cmd=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-I',str(d),str(d/'GunyahWatchdog.c'),str(p/'test-vdog.c'),'-o',str(p/'test-vdog')]
q=subprocess.run(cmd,capture_output=True,text=True);(p/'vdog-host-build.txt').write_text(q.stdout+q.stderr);assert q.returncode==0,q.stderr
q=subprocess.run([str(p/'test-vdog')],capture_output=True,text=True)
report={'returncode':q.returncode,'stdout':q.stdout,'stderr':q.stderr,'command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (d/'GunyahWatchdog.c',d/'GunyahWatchdog.h',p/'test-vdog.c')},'hardware_tested':False}
(p/'vdog-host.json').write_text(json.dumps(report,indent=2)+'\n');print(q.stdout);assert q.returncode==0 and not q.stderr,q.stderr
