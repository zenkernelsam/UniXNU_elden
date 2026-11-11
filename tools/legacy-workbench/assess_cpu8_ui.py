"""Assess the bounded UIKit window separately from whole-capture log loss."""
from pathlib import Path
import argparse,hashlib,json,re
from assess_ui_dispatch import analyze
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,default=r/'edl_work/live-ui-cpu8-swipe-v87-20261111');p.add_argument('--output',type=Path,default=r/'research/ui-dispatch-20261111/delivery-delay-v87.json');p.add_argument('--session',default='19fa368d1597c72f');p.add_argument('--visual-outcome',default='not assessed');args=p.parse_args();run=args.run.resolve()
assert re.fullmatch('[0-9a-f]{16}',args.session)
assert run.is_relative_to(r/'edl_work') and args.output.resolve().is_relative_to(r/'research')
logs=list((run/'usb').glob('*.bin'));assert len(logs)==1
b=logs[0].read_bytes();meta=json.loads(logs[0].with_suffix('.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert meta.get('raw_sha256')==sha(logs[0]) and meta['candidate_session']==args.session
text=b.decode(errors='replace');start=text.index('ELDEN_UI_DISPATCH arm=1 ')
match=re.search(r'ELDEN_UI_DISPATCH done [^\r\n]+',text);assert match and match.start()>start
status=re.compile(r'ELDEN STATUS v1 session='+re.escape(args.session)+r' [^\r\n]*?dropped=(\d+)[^\r\n]*')
before=list(status.finditer(text,0,start));after=list(status.finditer(text,match.end()));inside=list(status.finditer(text,start,match.end()))
assert before and after
drops=[int(before[-1][1]),*[int(x[1]) for x in inside],int(after[0][1])]
window_lossless=len(set(drops))==1 and not meta['discontinuities'] and not meta.get('oversized_console_lines_discarded')
a=analyze(text,window_lossless)
# Check record accounting independently; do not relabel a lossy transport window.
accounting=analyze(text,True);assert accounting['complete_observation']
timing=re.search(r'ELDEN_UI_TIMING down-call-us=(\d+) down-to-up-us=(\d+) up-call-us=(\d+)',text);assert timing
groups=[]
for e in a['events']:
 if not groups or groups[-1][0]['arg2']!=e['arg2']:groups.append([])
 groups[-1].append(e)
assert len(groups)==2 and all(len(x)==2 for x in groups)
assert len({(e['pid'],e['unique'],e['tid']) for e in a['events']})==1
out={'session':meta['candidate_session'],'boot_nonce':meta['boot_nonce'],'raw_sha256':sha(logs[0]),'bytes':len(b),
 'whole_capture_dropped':max(int(x['dropped']) for x in meta['statuses']),
 'window_status_dropped':drops,'window_complete':window_lossless,'event_records_complete':True,
 'event_accounting':'indices 0..3 and total=saved=4, overflow/revoked/emit-fail=0, filter restored; other log bytes may be lost',
 'observation':a,'hid_down_to_up_us':int(timing[2]),
 'uikit_group_interval_us':groups[1][0]['us']-groups[0][0]['us'],
 'event_ticks_difference':groups[1][0]['arg2']-groups[0][0]['arg2'],
 'event_interval_us_at_19_2MHz':(groups[1][0]['arg2']-groups[0][0]['arg2'])/19.2,
 'settings_content_verified':False,'physical_touch_verified':False,
 'observed_images':sorted(str(p.relative_to(run)) for p in (run/'trial-frames').glob('after-tap-*.png')),
 'visual_outcome':args.visual_outcome,
 'limits':['Generic sendEvent is not an explicit down/up trace','Boot/configuration differ from v77; no general speedup claim',
           'Whole capture has dropped bytes; the bounded window has stable drop counters' if window_lossless else 'Whole capture and observation interval have dropped bytes; only the four event records are complete by accounting',
           'No new full-charge, DVFS or long-term SMP qualification']}
trial=json.loads((run/'trial.json').read_text());out['desktop_settle_seconds']=trial['desktop_settle_seconds']
out['frame_sha256']={str(p.relative_to(r)).replace('\\','/'):sha(p) for p in (run/'trial-frames').glob('*.png')}
args.output.write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:out[k] for k in ('whole_capture_dropped','window_complete','hid_down_to_up_us','uikit_group_interval_us','event_interval_us_at_19_2MHz','settings_content_verified')},indent=2))
