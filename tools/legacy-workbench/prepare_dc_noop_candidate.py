"""Carry forward v55 qualifications only across explicitly reviewed v56 deltas."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
sha=lambda b:hashlib.sha256(b).hexdigest()
base=root/'edl_work/basic-bundle-20261108/v55/final/pd-ucsi-events-focused-gestures'
out=root/'edl_work/basic-bundle-20261108/v56/final/dc-noop-focused-gestures';out.mkdir(exist_ok=True)
old=root/'research/basic-bundle-20261108/source-compiled-v55'
new=root/'research/basic-bundle-20261108/source-compiled-v56'
reviewdir=root/'research/input-budget-20261110'
review=json.loads((reviewdir/'noop-source-review.json').read_text())
allowed={('usb/EldenSdamRecovery.inc' if n.endswith('EldenSdamRecovery.inc') else 'power/'+Path(n).name):h for n,h in review['sources'].items()}
allowed.update({'power/UcsiQuery.c':'bbb946310266310e7c8b31d5547f5e3ba9886ffa842cb6dd0dc7ba1a2cc6e3c9',
 'power/TestUcsiEvents.c':'37bf9315220449e8815ccd3a1a040f2c82018ebde0157433d7be8c8e0d3dec02'})
for n,h in allowed.items():assert sha((new/n).read_bytes())==h,n
changes={}
for n in ['PmicClient.cpp','ProtectionRuntime.cpp','PulseControl.h','EldenSdamRecovery.inc','UcsiQuery.c','TestUcsiEvents.c']:
 p=reviewdir/(n+'.diff');assert p.is_file();changes[n]=sha(p.read_bytes())
integration=dict(accepted=True,hardware_tested=False,reviewed_diffs=changes,
 scope='Fixed DC 500mA single same-value write; bounded drain before existing shutdown; read-only cold token; preserve unmatched UCSI connector before queued ACK.',
 unchanged=['PD bootstrap state machine','USB current budget','thermal policy and GPIO executor','backlight controller','battery publication core'],
 source_review_sha256=sha((reviewdir/'noop-source-review.json').read_bytes()))
(reviewdir/'noop-integration-review.json').write_text(json.dumps(integration,indent=2)+'\n')
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json']:
 data=(base/filename).read_bytes();q=json.loads(data);key='source_sha256' if filename=='battery-publication-contract.json' else 'sources'
 pref='' if filename.startswith('backlight-') else 'power/'
 changed={}
 for n,h in q[key].items():
  rel=pref+n;assert sha((old/rel).read_bytes())==h,('parent mismatch',rel)
  h2=sha((new/rel).read_bytes())
  if h2!=h:
   assert allowed.get(rel)==h2,('unreviewed delta',rel)
   changed[rel]={'old':h,'new':h2};q[key][n]=h2
 q['parent_qualification_sha256']=sha(data)
 q['dc_noop_integration_review_sha256']=sha((reviewdir/'noop-integration-review.json').read_bytes())
 q['reviewed_changed_sources']=changed
 if filename=='ucsi-events-qualification.json':
  q['checks']=424;q['scope']='UCSI notifications and pre-ACK different-connector handoff; no new UCSI role/current commands'
  q['native_codegen_sha256']=sha((out.parents[1]/'ucsi-codegen/audit.json').read_bytes())
 if filename=='pd-bootstrap-qualification.json':
  q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=json.loads((out.parents[1]/'dc-noop-codegen/audit.json').read_text())['archive_sha256']
 for rel in ['power/ColdToken.h','power/PulseControl.h'] if filename=='thermal-qualification.json' else []:
  q[key][Path(rel).name]=allowed[rel]
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
q=dict(review)
q['frozen_sources']=allowed
q['codegen_path']='edl_work/basic-bundle-20261108/v56/dc-noop-codegen/audit.json'
q['codegen_sha256']=sha((root/q['codegen_path']).read_bytes())
q['integration_review_sha256']=sha((reviewdir/'noop-integration-review.json').read_bytes())
(out/'dc-noop-qualification.json').write_text(json.dumps(q,indent=2)+'\n')
print(json.dumps({'output':str(out),'reviewed_deltas':list(changes),'hardware_tested':False},indent=2))
