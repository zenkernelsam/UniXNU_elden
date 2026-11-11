"""Finalize a successfully built icon bundle; audit/export only, no device I/O."""
from pathlib import Path
import argparse,subprocess,sys,hashlib
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);a=p.parse_args();assert 70<=a.version<=99
d=r/f'out/startup-basic-bundle-v{a.version}-20261108';assert (d/'result.json').exists()
jobs=[('audit_pd_bootstrap_mac.py',['--power-version','35','--version',str(a.version)]),
      ('audit_ucsi_events_mac.py',['--power-version','35','--version',str(a.version)]),
      ('audit_recovery_deadline_mac.py',['--version',str(a.version)]),
      ('audit_preview_codegen_mac.py',['--version',str(a.version)]),
      ('prepare_battery_telemetry_derivative.py',['--build-dir',str(d)]),
      ('export_basic_bundle_mac.py',['--version',str(a.version),'--power-version','35','--backlight-version','13'])]
if a.version>=71:jobs.insert(4,('audit_touch_dma_codegen_mac.py',['--version',str(a.version)]))
for name,args in jobs:
 with (d/(name+'.log')).open('x') as log:subprocess.run([sys.executable,r/'out'/name,*args],stdout=log,stderr=subprocess.STDOUT,check=True)
print('artifacts.zip SHA256='+hashlib.sha256((d/'artifacts.zip').read_bytes()).hexdigest(),flush=True)
