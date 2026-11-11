"""Summarize completed, SHA-bound read-only AW99706 observations; never admit writes."""
import argparse,hashlib,json,re
from pathlib import Path

def assess(root,session):
    files=list((root/'usb').glob('*.bin'));assert len(files)==1
    data=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text())
    assert meta.get('elapsed_seconds') is not None and meta['raw_sha256']==hashlib.sha256(data).hexdigest()
    text=data.decode('utf-8','replace')
    assert set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))=={session}
    assert meta['candidate_session']==session
    assert re.findall(r'ELDEN_BACKLIGHT inspect=-?\d+ mode=(\d+)',text)==['1'], 'Only the chip-read profile is observational'
    assert 'ELDEN_BACKLIGHT roundtrip=' not in text and 'ELDEN_BACKLIGHT published' not in text
    rows=[]
    for m in re.finditer(r'ELDEN_BACKLIGHT observation=(\d+) ms=(\d+) result=(-?\d+) open=([0-9a-f]+) short=([0-9a-f]+) flags=([0-9a-f]+) config=([0-9a-f]+) high=([0-9a-f]+) low=([0-9a-f]+) writes=(\d+) ready=(\d+)',text):
        v=m.groups();rows.append(dict(zip(['sample','ms','result','open','short','flags','config','high','low','writes','ready'],[int(x,16 if 3<=i<=8 else 10) for i,x in enumerate(v)])))
    complete=[r['ms'] for r in rows]==[100,1000,2000,4000,8000]
    clear=complete and all(r['result']==0 and r['open']==r['short']==r['flags']==r['writes']==0 for r in rows)
    stable=clear and len({(r['config'],r['high'],r['low']) for r in rows})==1
    lines=[s for s in text.splitlines() if s.startswith('ELDEN_BACKLIGHT')]
    return dict(session=session,raw_sha256=meta['raw_sha256'],bytes=len(data),
        initial_delay_ms=re.findall(r'ELDEN_BACKLIGHT first-read delay-ms=(\d+)',text),
        observations=rows,complete_five_samples=complete,all_later_flags_clear=clear,
        later_brightness_configuration_unchanged=stable,backlight_lines=lines,
        dropped_max=max(map(int,re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)),default=0),
        discontinuities=meta.get('discontinuities',[]),
        target_deadline_fired='ELDEN_RECOVERY DEADLINE_FIRED' in text,
        reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
        chip_write_admitted=False,read_clear_semantics_proven=False,brightness_working_proven=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--session',required=True);a=p.parse_args()
    report=assess(a.directory,a.session);(a.directory/'observation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
