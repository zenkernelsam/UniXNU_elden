"""Reuse the verified v59 runtime for one fixed DC500mA same-value test."""
from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=root/'edl_work/basic-bundle-20261108/v59/final/pd-handoff-quiet-focused-gestures'
out=base.with_name('dc-noop-quiet-focused-gestures');out.mkdir(exist_ok=True)
old=root/'research/basic-bundle-20261108/source-compiled-v56';new=root/'research/basic-bundle-20261108/source-compiled-v59'
core=['DcNoop.c','DcNoop.h','TestDcNoop.c','ColdToken.h','ProtectionRuntime.cpp','PulseControl.h','PowerPathRead.c','PowerPathRead.h']
for n in core:assert (old/'power'/n).read_bytes()==(new/'power'/n).read_bytes(),n
def dc_block(path):
 s=path.read_text();a=s.index('        if(dcEnable){\n            bool stopping=');b=s.index('\n        if(iclEnable==1) {',a);return s[a:b]
assert dc_block(old/'power/PmicClient.cpp')==dc_block(new/'power/PmicClient.cpp')
for name in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json','usb-handoff-qualification.json']:
 shutil.copyfile(base/name,out/name)
previous=root/'edl_work/basic-bundle-20261108/v56/final/dc-noop-focused-gestures/dc-noop-qualification.json'
q=json.loads(previous.read_text());q['parent_qualification_sha256']=sha(previous)
code=root/'edl_work/basic-bundle-20261108/v59/dc-noop-codegen/audit.json';native=json.loads(code.read_text());assert native['accepted']
frozen={'power/'+n:h for n,h in native['sources'].items()}
recovery=new/'usb/EldenSdamRecovery.inc';recovery_audit=json.loads((base.parents[1]/'recovery-codegen/audit.json').read_text())
assert sha(recovery)==recovery_audit['sources']['EldenSdamRecovery.inc'];frozen['usb/EldenSdamRecovery.inc']=sha(recovery)
q.update(frozen_sources=frozen,codegen_path='edl_work/basic-bundle-20261108/v59/dc-noop-codegen/audit.json',codegen_sha256=sha(code),
 unchanged_dc_core_and_executor=True,current_base_audit_sha256=sha(base/'independent-audit.json'),
 typed_sdp_qualification_sha256=sha(base/'usb-handoff-qualification.json'),pdo_events_qualification_sha256=sha(base/'ucsi-events-qualification.json'),hardware_tested=False)
(out/'dc-noop-qualification.json').write_text(json.dumps(q,indent=2)+'\n')
print('Core and executor identical to v56; v59 typed-SDP/DA65 recovery retained.')
