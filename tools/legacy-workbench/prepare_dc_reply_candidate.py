"""Prepare the v60 reply/ownership diagnostic; no DC policy change."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=root/'research/basic-bundle-20261108/source-compiled-v59';new=root/'research/basic-bundle-20261108/source-compiled-v60'
base=root/'edl_work/basic-bundle-20261108/v59/final/dc-noop-quiet-focused-gestures';out=root/'edl_work/basic-bundle-20261108/v60/final/dc-noop-quiet-focused-gestures';out.mkdir(exist_ok=True)
reviewpath=root/'research/dc-write-reply-20261110/source-review.json';review=json.loads(reviewpath.read_text());assert review['exact_existing_control_code_preserved'];allowed={'power/'+n:h for n,h in review['sources'].items()}
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json','usb-handoff-qualification.json']:
 q=json.loads((base/filename).read_text());key='source_sha256' if filename=='battery-publication-contract.json' else 'sources';prefix='' if filename.startswith('backlight-') else 'power/';changes={}
 for n,h in q[key].items():
  rel=prefix+n;assert sha(old/rel)==h
  updated=sha(new/rel)
  if updated!=h:assert allowed.get(rel)==updated,rel;changes[rel]={'old':h,'new':updated};q[key][n]=updated
 q['parent_qualification_sha256']=sha(base/filename);q['reviewed_changed_sources']=changes;q['dc_reply_review_sha256']=sha(reviewpath)
 if filename=='pd-bootstrap-qualification.json':q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=json.loads((out.parents[1]/'pd-codegen/audit.json').read_text())['archive_sha256']
 if filename=='ucsi-events-qualification.json':
  q['native_codegen_sha256']=sha(out.parents[1]/'ucsi-codegen/audit.json');q['pdo_notification_review_path']='research/dc-write-reply-20261110/pdo-preserved-review.json';q['pdo_notification_review_sha256']=sha(root/q['pdo_notification_review_path'])
 if filename=='usb-handoff-qualification.json':q['native_codegen_sha256']=sha(out.parents[1]/'pd-codegen/audit.json')
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
q=json.loads((base/'dc-noop-qualification.json').read_text());native_path=out.parents[1]/'dc-noop-codegen/audit.json';native=json.loads(native_path.read_text());assert native['accepted']
q['parent_qualification_sha256']=sha(base/'dc-noop-qualification.json');q['checks']=2033;q['codegen_path']='edl_work/basic-bundle-20261108/v60/dc-noop-codegen/audit.json';q['codegen_sha256']=sha(native_path)
q['frozen_sources']={'power/'+n:h for n,h in native['sources'].items()};q['frozen_sources']['usb/EldenSdamRecovery.inc']=sha(new/'usb/EldenSdamRecovery.inc')
q['dc_reply_review_sha256']=sha(reviewpath);q['hardware_tested']=False
(out/'dc-noop-qualification.json').write_text(json.dumps(q,indent=2)+'\n');print('Prepared reply diagnostic and read-only ownership survey.')
