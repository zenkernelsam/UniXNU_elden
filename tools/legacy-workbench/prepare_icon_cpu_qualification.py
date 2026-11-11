"""Reuse frozen power35 evidence and exact unchanged physical-touch inputs."""
from pathlib import Path
import argparse,hashlib,json
r=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--version',type=int,default=62);version=p.parse_args().version
v=r/f'edl_work/basic-bundle-20261108/v{version}';out=v/'final/icon-cpu-focused-gestures';out.mkdir(exist_ok=True)
src=r/f'research/basic-bundle-20261108/source-compiled-v{version}'
base=r/'edl_work/basic-bundle-20261108/v59/final/pd-handoff-quiet-focused-gestures'
for name in ['thermal-qualification.json','pd-qualification.json','ucsi-events-qualification.json','usb-handoff-qualification.json']:
 q=json.loads((base/name).read_text())
 for n,h in q['sources'].items():assert sha(src/'power'/n)==h,('changed power source',n)
 q['parent_qualification_sha256']=sha(base/name);q['power_sources_byte_equal']=True
 if name=='ucsi-events-qualification.json':q['native_codegen_sha256']=sha(v/'ucsi-codegen/audit.json')
 if name=='usb-handoff-qualification.json':q['native_codegen_sha256']=sha(v/'pd-codegen/audit.json')
 (out/name).write_text(json.dumps(q,indent=2)+'\n')
old=r/'edl_work/basic-bundle-20261108/v38/final/touch-event-id-replay-focused-gestures/touch-dma-qualification.json';q=json.loads(old.read_text());changed={}
preview=json.loads((v/'preview-codegen.json').read_text());assert preview['accepted']
observer=None
event_time=None
ui_dispatch=None
if version>=71:
 observer_path=r/'research/icon-launch-constraint-20261111/v71-observer-delta.json'
 observer=json.loads(observer_path.read_text());assert observer['accepted'] and observer['compiled_snapshot'] and observer['controls_unchanged']
if version==73:
 event_time_path=r/'research/hid-event-time-20261111/v73-source-delta.json'
 event_time=json.loads(event_time_path.read_text());assert event_time['accepted'] and event_time['compiled_snapshot'] and event_time['controls_unchanged']
if version>=74:
 ui_dispatch_path=r/'research/ui-dispatch-20261111'/f'v{version}-source-delta.json'
 ui_dispatch=json.loads(ui_dispatch_path.read_text());assert ui_dispatch['accepted'] and ui_dispatch['compiled_snapshot'] and ui_dispatch['controls_unchanged']
for n,h in q['sources'].items():
 now=sha(src/'tree'/n)
 if now!=h:
  if observer and n in ('scripts/driver-patches/EldenHidGlue.cpp','scripts/driver-patches/EldenUserWaitProbe.c'):
   if ui_dispatch and n=='scripts/driver-patches/EldenHidGlue.cpp':
    previous=observer['files']['tree/'+n];current=ui_dispatch['files']['tree/'+n]
    assert previous['old']==h and previous['new']==current['old'] and current['new']==now
   elif event_time and n=='scripts/driver-patches/EldenUserWaitProbe.c':
    previous=observer['files']['tree/'+n];current=event_time['files']['tree/'+n]
    assert previous['old']==h and previous['new']==current['old'] and current['new']==now
   else:assert observer['files']['tree/'+n]==dict(old=h,new=now)
  else:assert n=='scripts/driver-patches/EldenDisplayGlue.cpp' and now==preview['source_sha256']
  changed[n]={'old':h,'new':now};q['sources'][n]=now
q['parent_qualification_sha256']=sha(old);q['reviewed_delta']=dict(changed=changed,scope='Existing qualified native statusbar crop; physical touch/HID/wait code unchanged',preview_codegen_sha256=sha(v/'preview-codegen.json'))
if observer:
 q['reviewed_delta'].update(scope='Qualified statusbar plus bounded target-wait and software-tap timing; existing touch controls byte-identical after removal of observations',observer_delta_sha256=sha(observer_path))
 native_path=v/'touch-codegen/audit.json';native=json.loads(native_path.read_text());assert native['accepted']
 assert {'EldenUserWaitProbe','EldenHidGlue'}<=set(native['objects'])
 q['reviewed_delta']['observer_native_codegen_sha256']=sha(native_path)
 if event_time:
  q['reviewed_delta']['event_time_delta_sha256']=sha(event_time_path)
  q['reviewed_delta']['scope']+='; read-only event and user commpage clock observations'
 if ui_dispatch:
  q['reviewed_delta']['ui_dispatch_delta_sha256']=sha(ui_dispatch_path)
  q['reviewed_delta']['scope']+='; one-shot UIKit dispatch window, existing input publication unchanged'
(out/'touch-dma-qualification.json').write_text(json.dumps(q,indent=2)+'\n');print('Reused exact power35 and unchanged touch transport/controls; reviewed observation deltas bound.')
