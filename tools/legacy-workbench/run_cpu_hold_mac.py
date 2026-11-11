from pathlib import Path
import hashlib,json,subprocess,sys,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');p=r/'research/cpu-hold-20261111';assert Path.cwd()==r
with zipfile.ZipFile(p/'source.zip') as z:
 m=json.loads(z.read('source.json'))
 for n,h in m['files'].items():
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h;(p/n).write_bytes(b)
cmd=['/usr/bin/clang','-std=c11','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'TestHoldPolicy.c'),'-o',str(p/'test-hold')]
subprocess.run(cmd,check=True);test=subprocess.run([p/'test-hold'],capture_output=True,text=True,check=True)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
(p/'host-policy.json').write_text(json.dumps({'passed':True,'stdout':test.stdout,'stderr':test.stderr,'test_sha256':sha(p/'TestHoldPolicy.c'),'policy_sha256':sha(p/'HoldPolicy.h'),'sanitizers':['address','undefined']},indent=2)+'\n')
print(test.stdout,flush=True)
subprocess.run([sys.executable,p/'relink_cpu_groups_mac.py','--version','87'],check=True)
subprocess.run([sys.executable,r/'out/prepare_battery_telemetry_derivative.py','--attempt','3','--build-dir',r/'out/startup-basic-bundle-v87-20261108'],check=True)
subprocess.run([sys.executable,p/'export_basic_bundle_mac.py','--version','87','--power-version','35','--backlight-version','13','--cpu-irq-snapshot'],check=True)
