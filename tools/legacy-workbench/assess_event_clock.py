"""Interpret captured event clocks; this reports evidence, never changes clocks."""
from pathlib import Path
import argparse,hashlib,json,re
MASK=(1<<64)-1
def projection(selector,offset,counter,before,after,stable=True):
    if not stable or before>after:return dict(result='unstable')
    if selector==0:return dict(result='kernel-trap-fallback')
    if selector not in (1,2):return dict(result='unsupported-selector')
    value=(offset+counter)&MASK
    delta=((value-before+(1<<63))&MASK)-(1<<63)
    return dict(result='within-kernel-window' if before<=value<=after else 'outside-kernel-window',
                projected=value,delta_from_before_ticks=delta,window_ticks=after-before)
def main():
    p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    paths=list((a.run/'usb').glob('*.bin'));assert len(paths)==1
    b=paths[0].read_bytes();meta=json.loads(paths[0].with_suffix('.json').read_text());text=b.decode(errors='replace')
    digest=hashlib.sha256(b).hexdigest();complete=bool(meta.get('raw_sha256'))
    if complete:assert digest==meta['raw_sha256']
    clocks=[]
    pattern=r'ELDEN_USER_CLOCK pass=(\d+) name=(\S+) pid=(\d+) error=(-?\d+)/(-?\d+) stable=(\d+) selector=(\d+) offset=([0-9a-f]+) vct=([0-9a-f]+) before=([0-9a-f]+) after=([0-9a-f]+)'
    for m in re.finditer(pattern,text):
        row=dict(pass_=int(m[1]),name=m[2],pid=int(m[3]),errors=[int(m[4]),int(m[5])],stable=int(m[6]),selector=int(m[7]),
                 offset=int(m[8],16),vct=int(m[9],16),before=int(m[10],16),after=int(m[11],16))
        row['projection']=projection(row['selector'],row['offset'],row['vct'],row['before'],row['after'],row['stable']==1 and row['errors']==[0,0]);clocks.append(row)
    events=[];malformed=0
    for line in text.splitlines():
        if not line.startswith('ELDEN_HID_EVENT '):continue
        fields=dict(part.split('=',1) for part in line.split()[1:] if '=' in part)
        if not {'n','child','touch','time','now','raw-time','options'}<=fields.keys():malformed+=1;continue
        try:row={k:int(v,16 if k in ('time','now','raw-time','options','mask') else 10) for k,v in fields.items()}
        except ValueError:malformed+=1;continue
        row['age_ticks']=row['now']-row['time'];row['age_us']=row['age_ticks']/19.2
        events.append(row)
    strokes=[];start=None
    for row in (e for e in events if e['child']==-1):
        if row['touch'] and start is None:start=row
        if not row['touch'] and start is not None:
            strokes.append(dict(begin_n=start['n'],end_n=row['n'],duration_ticks=row['time']-start['time'],duration_us=(row['time']-start['time'])/19.2));start=None
    result=dict(session=meta.get('candidate_session'),raw_sha256=digest,bytes=len(b),capture_finished=complete,
                dropped=max((int(x['dropped']) for x in meta.get('statuses',[])),default=None),
                timebase_hz=19200000,clock_samples=clocks,event_samples=events,strokes=strokes,malformed_event_lines=malformed,
                note='Projection uses exact 21H461 user fast-path formula; selector2 is the architectural self-synchronizing virtual counter. Does not prove UIKit consumed an event.')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(clocks=len(clocks),projection_results=[x['projection']['result'] for x in clocks],events=len(events),strokes=strokes,
                         max_event_age_us=max((x['age_us'] for x in events),default=None),dropped=result['dropped']),indent=2))
if __name__=='__main__':main()
