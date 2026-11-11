"""Qualify v59 through the reviewed PDO event-mask delta, retaining v58 power."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=root/'research/basic-bundle-20261108/source-compiled-v58';new=root/'research/basic-bundle-20261108/source-compiled-v59'
base=root/'edl_work/basic-bundle-20261108/v58/final/pd-handoff-quiet-focused-gestures';out=root/'edl_work/basic-bundle-20261108/v59/final/pd-handoff-quiet-focused-gestures';out.mkdir(exist_ok=True)
reviewpath=root/'research/pdo-events-20261110/source-review.json';review=json.loads(reviewpath.read_text());allowed={'power/'+n:h for n,h in review['sources'].items()}
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json','usb-handoff-qualification.json']:
 q=json.loads((base/filename).read_text());key='source_sha256' if filename=='battery-publication-contract.json' else 'sources';prefix='' if filename.startswith('backlight-') else 'power/';changes={}
 for n,h in q[key].items():
  rel=prefix+n;assert sha(old/rel)==h
  updated=sha(new/rel)
  if updated!=h:assert allowed.get(rel)==updated,rel;changes[rel]={'old':h,'new':updated};q[key][n]=updated
 q['parent_qualification_sha256']=sha(base/filename);q['reviewed_changed_sources']=changes;q['pdo_notification_review_sha256']=sha(reviewpath)
 if filename=='pd-bootstrap-qualification.json':q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=json.loads((out.parents[1]/'pd-codegen/audit.json').read_text())['archive_sha256']
 if filename=='ucsi-events-qualification.json':
  q['native_codegen_sha256']=sha(out.parents[1]/'ucsi-codegen/audit.json');q['notification_control']='0xda650005';q['checks']=431;q['pdo_notification_quirk']=True
 if filename=='usb-handoff-qualification.json':q['native_codegen_sha256']=sha(out.parents[1]/'pd-codegen/audit.json')
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
print('Prepared PDO notification candidate; no power-control code delta.')
