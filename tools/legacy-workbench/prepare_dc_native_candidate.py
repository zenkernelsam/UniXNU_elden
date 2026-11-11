"""Prepare v61 only: reviewed native DC delta, no qualification blanket update."""
from pathlib import Path
import hashlib,json,difflib
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=root/'research/basic-bundle-20261108/source-compiled-v60';new=root/'research/basic-bundle-20261108/source-compiled-v61'
base=root/'edl_work/basic-bundle-20261108/v60/final/dc-noop-quiet-focused-gestures';v=root/'edl_work/basic-bundle-20261108/v61'
out=v/'final/dc-native-trial-focused-gestures';out.mkdir(exist_ok=True)
rd=root/'research/dc-native-20261111';rd.mkdir(exist_ok=True)
native=json.loads((v/'dc-native-codegen/audit.json').read_text());assert native['accepted']
allowed={'power/'+n:h for n,h in native['sources'].items()}
allowed['usb/EldenSdamRecovery.inc']='3c3c39b4e82ef7cc2bb95ea0a5e49b6b72e4787a793caf7f758bcfd243e587bf'
for n,h in allowed.items():assert sha(new/n)==h,n
diffs={}
for n in allowed:
 before=(old/n).read_text() if (old/n).exists() else '';after=(new/n).read_text()
 if before!=after:
  p=rd/(Path(n).name+'.diff');p.write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='v60/'+n,tofile='v61/'+n)),encoding='utf8');diffs[n]=sha(p)
for n in ['PdBootstrap.c','UsbIcl.c','ProtectionRuntime.cpp','UcsiQuery.c','ChargePolicy.c','ChargeVotes.c','PowerPathRead.c','NativeBackend.h']:
 assert (old/'power'/n).read_bytes()==(new/'power'/n).read_bytes(),n
review=dict(accepted=True,hardware_tested=False,sources=allowed,diffs=diffs,
 scope='Opt-in native AP DC450/500 only; existing SDP, PDO, PD bootstrap and thermal policy unchanged; active-trial votes refreshed at 1s; native restore precedes existing shutdown.',
 trial_checks=1432,spmi_checks=50036,maximum_ma=500,low_ma=450,
 recovery='6s fresh-guard restore wait; 9s stop drain then atomic abort and writer mutex fence; original firmware clamp wins; no write retry.')
reviewpath=rd/'source-review.json';reviewpath.write_text(json.dumps(review,indent=2)+'\n')
pdo=json.loads((root/'research/dc-write-reply-20261110/pdo-preserved-review.json').read_text())
for n in pdo['sources']:pdo['sources'][n]=sha(new/'power'/n)
pdo.update(scope='PDO notification selection/query/ACK code unchanged. Native DC controls qualified separately.',other_power_inputs_byte_equal=False,native_dc_review_sha256=sha(reviewpath))
pdopath=rd/'pdo-preserved-review.json';pdopath.write_text(json.dumps(pdo,indent=2)+'\n')
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json','usb-handoff-qualification.json']:
 q=json.loads((base/filename).read_text());key='source_sha256' if filename=='battery-publication-contract.json' else 'sources';prefix='' if filename.startswith('backlight-') else 'power/';changes={}
 for n,h in q[key].items():
  rel=prefix+n;assert sha(old/rel)==h;updated=sha(new/rel)
  if updated!=h:assert allowed.get(rel)==updated,rel;changes[rel]={'old':h,'new':updated};q[key][n]=updated
 q['parent_qualification_sha256']=sha(base/filename);q['reviewed_changed_sources']=changes;q['native_dc_review_sha256']=sha(reviewpath)
 if filename=='pd-bootstrap-qualification.json':q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=native['archive_sha256']
 if filename=='ucsi-events-qualification.json':
  q['native_codegen_sha256']=sha(v/'ucsi-codegen/audit.json');q['pdo_notification_review_path']=pdopath.relative_to(root).as_posix();q['pdo_notification_review_sha256']=sha(pdopath)
 if filename=='usb-handoff-qualification.json':q['native_codegen_sha256']=sha(v/'pd-codegen/audit.json');q['scope']='Unchanged typed SDP implementation; additional native DC feature separately qualified.'
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
q=dict(accepted=True,hardware_tested=False,checks=1432,spmi_checks=50036,frozen_sources=allowed,
 target=dict(bus=0,sid=7,address='0x2a52',raw_values=[9,10],maximum_ma=500,low_ma=450,max_writes=2),
 real_spmi_completion=True,stop_writer_lock=True,higher_current_supported=False,
 codegen_path=(v/'dc-native-codegen/audit.json').relative_to(root).as_posix(),codegen_sha256=sha(v/'dc-native-codegen/audit.json'),
 source_review_path=reviewpath.relative_to(root).as_posix(),source_review_sha256=sha(reviewpath))
(out/'dc-native-qualification.json').write_text(json.dumps(q,indent=2)+'\n')
print(json.dumps({'output':str(out),'changed_sources':list(diffs),'hardware_tested':False},indent=2))
