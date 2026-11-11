"""Compare complete PMIC samples across marked backlight load phases, read-only."""
from pathlib import Path
import argparse,hashlib,json,re,statistics
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--session',required=True);a=p.parse_args()
files=list((a.directory/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text())
assert meta.get('elapsed_seconds') is not None and meta['raw_sha256']==hashlib.sha256(raw).hexdigest()
text=raw.decode('utf-8','replace');assert meta['candidate_session']==a.session
assert set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))=={a.session}
assert re.findall(r'ELDEN_BACKLIGHT inspect=-?\d+ mode=(\d+)',text)==['5']
phase='startup';pending=None;phases=[];samples=[]
for line in text.splitlines():
    if line.startswith('ELDEN_BACKLIGHT power-finished='):phase='post-trial'
    m=re.fullmatch(r'ELDEN_BACKLIGHT load-phase=(\w+) time-us=(\d+) level=(\d+) hold-ms=(\d+)',line)
    if m:
        phase=m[1]
        if phase!='settle':phases.append(dict(phase=phase,time_us=int(m[2]),level=int(m[3]),hold_ms=int(m[4])))
    m=re.fullmatch(r'ELDEN_PMIC cycle=(\d+) epoch=(\d+) valid=([0-9a-f]+) usb-icl-sets=(\d+) voltage-raw=(\d+) current-raw=(-?\d+) temperature-raw=(-?\d+)',line)
    if m:pending=dict(phase=phase,cycle=int(m[1]),epoch=int(m[2]),valid=int(m[3],16),voltage_uv=int(m[5]),current_ua=int(m[6]),temperature_raw=int(m[7]))
    m=re.fullmatch(r'ELDEN_PMIC usb1=(\d+) usb2=(\d+) suspend=(\d+) charging-enable=(\d+) pps=(\d+); fg1-v=(\d+) fg2-v=(\d+) fg1-i=(-?\d+) fg2-i=(-?\d+)',line)
    if m and pending:
        pending.update(fg1_ma=int(m[8]),fg2_ma=int(m[9]),fg1_uv=int(m[6]),fg2_uv=int(m[7]));samples.append(pending);pending=None
groups={}
for name in ['baseline','dim','restored']:
    rows=[x for x in samples if x['phase']==name and x['valid']==(1<<45)-1]
    # Exclude initial cycles after each transition. These are cycle counts,
    # not an assumed wall-clock duration or a time-weighted energy estimate.
    tail=[x for x in rows if rows and x['cycle']>=rows[0]['cycle']+6]
    currents=[x['current_ua'] for x in tail]
    groups[name]=dict(total_samples=len(rows),after_six_cycles=len(tail),samples=tail,
        median_current_ua=statistics.median(currents) if currents else None,
        mean_current_ua=statistics.mean(currents) if currents else None,
        range_current_ua=[min(currents),max(currents)] if currents else None,
        all_coherent_net_charge=len(tail)>=6 and all(x['current_ua']<0 and x['fg1_ma']>0 and x['fg2_ma']>0 for x in tail))
report=dict(session=a.session,raw_sha256=meta['raw_sha256'],bytes=len(raw),phases=phases,groups=groups,
    finish=re.findall(r'ELDEN_BACKLIGHT power-finished=[^\r\n]*',text),
    backlight_stop=re.findall(r'ELDEN_BACKLIGHT stop=[^\r\n]*',text),
    source_mode=re.findall(r'ELDEN_BACKLIGHT cfg7=[^\r\n]*',text),
    dropped=max(map(int,re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)),default=0),
    discontinuities=meta.get('discontinuities',[]),
    reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
    target_deadline_fired='ELDEN_RECOVERY DEADLINE_FIRED' in text,
    interpretation='Aggregate negative and both FG positive supports sampled net charging. Means are unweighted; no per-port power, optical brightness, peak charging or full-goal claim.')
(a.directory/'backlight-power-assessment.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({**report,'groups':{k:{n:v for n,v in g.items() if n!='samples'} for k,g in groups.items()}},indent=2))
