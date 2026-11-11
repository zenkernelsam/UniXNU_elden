"""Reuse unchanged v86 module proofs, and run codegen audits for the new link."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
r=Path(__file__).resolve().parents[1];old=r/'edl_work/basic-bundle-20261108/v86';new=r/'edl_work/basic-bundle-20261108/v87'
osrc=r/'research/basic-bundle-20261108/source-compiled-v86';nsrc=r/'research/basic-bundle-20261108/source-compiled-v87'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
om=json.loads((osrc/'manifest.json').read_text())['files'];nm=json.loads((nsrc/'manifest.json').read_text())['files']
assert set(nm)-set(om)=={'cpu/HoldPolicy.h','cpu/TestHoldPolicy.c'} and not set(om)-set(nm)
changes={n for n,h in om.items() if nm[n]!=h}
assert changes=={'cpu/SmpGuard.inc','usb/EldenSdamRecovery.inc','build-scripts/relink_cpu_groups_mac.py'},changes
for path in (old/'final/archives').rglob('*'):
 if path.is_file():assert path.read_bytes()==(new/'final/archives'/path.relative_to(old/'final/archives')).read_bytes(),path
assert (old/'final/display-build-report.json').read_bytes()==(new/'final/display-build-report.json').read_bytes()
copies={}
for folder in ('pd-codegen','ucsi-codegen','touch-codegen'):
 target=new/folder;target.mkdir(exist_ok=True)
 for f in (old/folder).iterdir():
  if f.is_file():
   out=target/f.name
   if out.exists():assert out.read_bytes()==f.read_bytes()
   else:shutil.copyfile(f,out)
 copies[folder]=sha(old/folder/'audit.json')
shutil.copyfile(old/'preview-codegen.json',new/'preview-codegen.json')
copies['preview-codegen']=sha(old/'preview-codegen.json')
for script in ('audit_cpu_asm_fixed.py','audit_icon_cpu_codegen.py','audit_ui_dispatch.py','prepare_icon_cpu_qualification.py'):
 result=subprocess.run([sys.executable,r/'edl_work'/script,'--version','87'],capture_output=True,text=True)
 (new/(script+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(script,result.stderr[-1800:])
 print(script+' passed',flush=True)
out=new/'final/icon-cpu-focused-gestures-ipad-pro-cpu8';out.mkdir(exist_ok=True)
for f in (new/'final/icon-cpu-focused-gestures').glob('*-qualification.json'):shutil.copyfile(f,out/f.name)
(new/'reused-qualifications.json').write_text(json.dumps({'archive_bytes_equal':True,'changed_sources':sorted(changes),'unchanged_module_proofs':copies},indent=2)+'\n')
