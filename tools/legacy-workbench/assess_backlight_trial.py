"""Evaluate the bounded mode4 chip-register trial from a completed bound capture."""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--session',required=True);a=p.parse_args()
paths=list((a.directory/'usb').glob('*.bin'));assert len(paths)==1
data=paths[0].read_bytes();meta=json.loads(paths[0].with_suffix('.json').read_text())
assert meta.get('elapsed_seconds') is not None and meta['raw_sha256']==hashlib.sha256(data).hexdigest()
text=data.decode('utf-8','replace');assert meta['candidate_session']==a.session
assert set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))=={a.session}
assert re.findall(r'ELDEN_BACKLIGHT inspect=-?\d+ mode=(\d+)',text)==['4']
lines=[line for line in text.splitlines() if line.startswith('ELDEN_BACKLIGHT')]
qualified='ELDEN_BACKLIGHT trial-qualified=0 samples=5 level=1597 writes=0' in lines
roundtrip='ELDEN_BACKLIGHT roundtrip=0 before=1597 dim=798 restored=1597 chip-writes=4' in lines
released=len(re.findall(r'ELDEN_BACKLIGHT release=0 quarantine=0 transfers=\d+ chip-writes=4',text))==1
dropped=max(map(int,re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)),default=0)
report=dict(session=a.session,bytes=len(data),raw_sha256=meta['raw_sha256'],lines=lines,
    qualification_passed=qualified,register_roundtrip_passed=roundtrip,ownership_released=released,
    register_trial_passed=qualified and roundtrip and released and dropped==0 and not meta.get('discontinuities'),
    dropped_max=dropped,discontinuities=meta.get('discontinuities',[]),
    target_deadline_fired='ELDEN_RECOVERY DEADLINE_FIRED' in text,
    reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
    visual_brightness_confirmed=False,corebrightness_qualified=False,power_savings_measured=False)
(a.directory/'trial-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
