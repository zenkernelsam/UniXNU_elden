"""Interpret one completed, session-bound UI capture; never operate the device."""
import argparse,hashlib,json,re
from pathlib import Path
EVENT=re.compile(r'ELDEN_UI_DISPATCH event=(\d+) name=(SpringBoard|Preferences) pid=(\d+) unique=(\d+) tid=(\d+) us=(\d+) arg1=([0-9a-f]+) arg2=([0-9a-f]+)')
DONE=re.compile(r'ELDEN_UI_DISPATCH done total=(\d+) saved=(\d+) overflow=(\d+) springboard=(\d+) preferences=(\d+) filter-restored=(\d+) revoked=(\d+) emit-fail=(\d+)')
ARM=re.compile(r'ELDEN_UI_DISPATCH arm=(-?\d+) window-seconds=30')
def analyze(text,transport_complete):
    events=[dict(index=int(m[1]),name=m[2],pid=int(m[3]),unique=int(m[4]),tid=int(m[5]),us=int(m[6]),arg1=int(m[7],16),arg2=int(m[8],16)) for m in EVENT.finditer(text)]
    # Exact UIKitCore sendEvent call site, not a guess about HID touch phases.
    # Bits26/27 are _isPhysicalKeyEvent/_isTouchRoutingPolicyBased; count is
    # packed into bits16..25, with subtype/type in the low two bytes.
    for e in events:
        e['event_type']=e['arg1']&255
        e['subtype']=(e['arg1']>>8)&255
        e['collection_count']=(e['arg1']>>16)&1023
        e['physical_key_event']=bool(e['arg1']&(1<<26))
        e['touch_routing_policy_based']=bool(e['arg1']&(1<<27))
    arms=[int(m[1]) for m in ARM.finditer(text)]
    summaries=[dict(zip(('total','saved','overflow','springboard','preferences','restored','revoked','emit_fail'),map(int,m.groups()))) for m in DONE.finditer(text)]
    malformed=text.count('ELDEN_UI_DISPATCH ')-len(events)-len(arms)-len(summaries)
    complete=False
    if len(summaries)==1:
        s=summaries[0]
        complete=(transport_complete and arms==[1] and malformed==0 and s['restored']==1 and s['revoked']==0 and
                  s['overflow']==0 and s['emit_fail']==0 and s['saved']==s['total']==len(events) and
                  s['springboard']+s['preferences']==s['total'] and
                  [e['index'] for e in events]==list(range(s['saved'])) and
                  all(0<=e['us']<=31_000_000 and e['arg1']&255==0 for e in events) and
                  sum(e['name']=='SpringBoard' for e in events)==s['springboard'] and
                  sum(e['name']=='Preferences' for e in events)==s['preferences'])
    return dict(complete_observation=complete,arms=arms,summaries=summaries,events=events,malformed_markers=malformed,
        springboard_dispatch_observed=any(e['name']=='SpringBoard' for e in events),
        preferences_dispatch_observed=any(e['name']=='Preferences' for e in events),
        settings_content_verified=False,physical_touch_verified=False,
        next_evidence='Use the same capture UI frames, process lifecycle and IconServices results. Generic sendEvent is not a generic down/up pair, and absence alone does not identify a missing driver.')
def main():
    p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--session',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
    raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text());digest=hashlib.sha256(raw).hexdigest()
    assert meta.get('candidate_session')==a.session
    if meta.get('raw_sha256'):assert meta['raw_sha256']==digest
    text=raw.decode(errors='replace');assert 'ELDEN STATUS v1 session='+a.session in text
    drops=max((int(x['dropped']) for x in meta.get('statuses',[])),default=None)
    complete=bool(meta.get('raw_sha256')) and drops==0 and not meta.get('discontinuities')
    result=analyze(text,complete);result.update(session=a.session,raw_sha256=digest,bytes=len(raw),dropped=drops,transport_complete=complete)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('complete_observation','springboard_dispatch_observed','preferences_dispatch_observed','dropped')},indent=2))
if __name__=='__main__':main()
