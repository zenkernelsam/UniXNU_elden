"""Carry v57 qualifications through only the reviewed SDP guard changes."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=root/'research/basic-bundle-20261108/source-compiled-v57';new=root/'research/basic-bundle-20261108/source-compiled-v58'
base=root/'edl_work/basic-bundle-20261108/v57/final/pd-policy-trace-focused-gestures';out=root/'edl_work/basic-bundle-20261108/v58/final/pd-handoff-focused-gestures';out.mkdir(exist_ok=True)
reviewpath=root/'research/pd-handoff-20261110/source-review.json';review=json.loads(reviewpath.read_text());allowed={'power/'+n:row['new'] for n,row in review['changed'].items()}
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json','ulog-qualification.json']:
 q=json.loads((base/filename).read_text());key='source_sha256' if filename=='battery-publication-contract.json' else 'sources';prefix='' if filename.startswith('backlight-') else 'power/';changes={}
 for n,h in q[key].items():
  rel=prefix+n;assert sha(old/rel)==h
  updated=sha(new/rel)
  if updated!=h:assert allowed.get(rel)==updated,rel;changes[rel]={'old':h,'new':updated};q[key][n]=updated
 q['parent_qualification_sha256']=sha(base/filename);q['reviewed_changed_sources']=changes;q['handoff_review_sha256']=sha(reviewpath)
 if filename=='pd-bootstrap-qualification.json':q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=json.loads((out.parents[1]/'pd-codegen/audit.json').read_text())['archive_sha256']
 if filename=='ucsi-events-qualification.json':q['native_codegen_sha256']=sha(out.parents[1]/'ucsi-codegen/audit.json')
 if filename=='ulog-qualification.json':q['native_codegen_sha256']=sha(out.parents[1]/'ulog-filter-codegen/audit.json');assert sha(new/'usb/EldenSdamRecovery.inc')==q['recovery_source_sha256'];q['scope']='ULog code unchanged; typed USB command guard has a separate qualification.'
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
q=dict(review);q['sources']={n:sha(new/'power'/n) for n in ['PmicClient.cpp','UsbIcl.c','UsbIcl.h','TestUsbIcl.c','TestUsbHandoff.c']};q['native_codegen_sha256']=sha(out.parents[1]/'pd-codegen/audit.json')
(out/'usb-handoff-qualification.json').write_text(json.dumps(q,indent=2)+'\n');print('Prepared typed SDP guard candidate qualifications.')
