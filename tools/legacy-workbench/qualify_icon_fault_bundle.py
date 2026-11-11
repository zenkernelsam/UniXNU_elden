from pathlib import Path
import hashlib,json,shutil,subprocess,sys
r=Path(__file__).resolve().parents[1];old=r/'edl_work/basic-bundle-20261108/v88';new=r/'edl_work/basic-bundle-20261108/v89'
before=r/'research/basic-bundle-20261108/source-compiled-v88';after=r/'research/basic-bundle-20261108/source-compiled-v89';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=json.loads((before/'manifest.json').read_text())['files'];b=json.loads((after/'manifest.json').read_text())['files']
assert {n for n in a if a[n]!=b[n]}=={'usb/EldenUsbDebugXnu.inc'}
assert set(b)-set(a)=={'usb/FaultFrames.h','usb/TestFaultFrames.c','usb/IconFaultTrace.inc','usb/IconFaultCapture.c','build-scripts/relink_icon_fault_mac.py','build-scripts/build_icon_fault_trace_mac.py'}
for folder in ('archives','display-archives'):
 for p in (old/'final'/folder).rglob('*'):
  if p.is_file():assert p.read_bytes()==(new/'final'/folder/p.relative_to(old/'final'/folder)).read_bytes()
assert (old/'final/cpu-archives/ArmCpu.o').read_bytes()==(new/'final/cpu-archives/ArmCpu.o').read_bytes()
for folder in ('pd-codegen','ucsi-codegen','touch-codegen'):
 (new/folder).mkdir(exist_ok=True)
 for p in (old/folder).iterdir():
  if p.is_file():shutil.copyfile(p,new/folder/p.name)
preview=json.loads((old/'preview-codegen.json').read_text())
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
kernel=(new/'final/kernel.macho').read_bytes();preview['linked_helper_address']=hex(m.symbols(kernel)['_elden_preview_after_input']);preview['reused_v88_codegen_sha256']=sha(old/'preview-codegen.json')
(new/'preview-codegen.json').write_text(json.dumps(preview,indent=2)+'\n')
for script in ('audit_icon_fault_trace.py','audit_cpu_asm_fixed.py','audit_icon_cpu_codegen.py','audit_ui_dispatch.py','prepare_icon_cpu_qualification.py'):
 cmd=[sys.executable,r/'edl_work'/script]+([] if script=='audit_icon_fault_trace.py' else ['--version','89'])
 q=subprocess.run(cmd,capture_output=True,text=True);(new/(script+'.log')).write_text(q.stdout+q.stderr)
 assert q.returncode==0,(script,q.stderr[-2000:]);print(script+' passed',flush=True)
out=new/'final/icon-cpu-focused-gestures-ipad-pro-cpu8';out.mkdir(exist_ok=True)
for p in (new/'final/icon-cpu-focused-gestures').glob('*-qualification.json'):shutil.copyfile(p,out/p.name)
(new/'reused-qualifications.json').write_text(json.dumps({'unchanged':'v88 power,display,touch and ArmCpu objects','only_existing_source_changed':'usb/EldenUsbDebugXnu.inc','new_objects':['IconFaultCapture.o'],'fault_codegen_sha256':sha(new/'fault-codegen.json')},indent=2)+'\n')
