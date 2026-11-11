"""Summarize stable native charge-mode intervals from the recorded CDC stream."""
import argparse,hashlib,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
files=list((a.directory/'usb').glob('*.bin'));assert len(files)==1
data=files[0].read_bytes();text=data.decode('utf-8','replace');rows={};cycle=0;segments=[]
active=dict(mode=0,start=1)
def close():
    global active
    if active is not None:active['end']=cycle;segments.append(active);active=None
for line in text.splitlines():
    if line.startswith('ELDEN ACK CHARGE '):close()
    m=re.search(r'ELDEN_CHARGE applied mode=(\d+) window=(\d+).*enabled=(\d+).*cycle=(\d+)',line)
    if m:
        close();active=dict(mode=int(m[1]),window=int(m[2]),start=int(m[4]),applied_enable=int(m[3]))
    m=re.search(r'ELDEN_PMIC cycle=(\d+).*voltage-raw=(\d+) current-raw=(-?\d+) temperature-raw=(-?\d+)',line)
    if m:
        cycle=int(m[1]);rows[cycle]=dict(cycle=cycle,voltage_uv=int(m[2]),current_ua=int(m[3]),temperature_raw=int(m[4]))
    m=re.search(r'ELDEN_PMIC usb1=.*charging-enable=(\d+).*fg1-i=(-?\d+) fg2-i=(-?\d+)',line)
    if m and cycle in rows:rows[cycle].update(enabled=int(m[1]),fg1_ma=int(m[2]),fg2_ma=int(m[3]))
    m=re.search(r'ELDEN_POWER_INPUTS USB-v=(\d+) i=(-?\d+) limit=(\d+)',line)
    if m and cycle in rows:rows[cycle].update(usb_uv=int(m[1]),usb_ua=int(m[2]),usb_limit_ua=int(m[3]))
    m=re.search(r'ELDEN_PMIC_RAW cycle=(\d+).*first=0 .*values=([0-9a-f,]+)',line)
    if m and int(m[1]) in rows:rows[int(m[1])]['soc_centi']=int(m[2].split(',')[4],16)
close();summary=[]
for segment in segments:
    group=[r for n,r in sorted(rows.items()) if max(4,segment['start'])<=n<=segment['end']][-6:]
    if not group:continue
    result={**segment,'samples':len(group),'cycles':[r['cycle'] for r in group]}
    for key in ['current_ua','fg1_ma','fg2_ma','usb_ua','soc_centi']:
        values=[r[key] for r in group if key in r]
        if values:result['median_'+key]=statistics.median(values)
    result['usb_limits']=sorted({r['usb_limit_ua'] for r in group if 'usb_limit_ua' in r})
    result['enabled_values']=sorted({r['enabled'] for r in group if 'enabled' in r})
    summary.append(result)
meta=json.loads(files[0].with_suffix('.json').read_text())
result=dict(log_sha256=hashlib.sha256(data).hexdigest(),log_bytes=len(data),segments=summary,
    mode_sequence_complete=meta.get('charge_sequence_complete'),restore_confirmed=meta.get('charge_restore_confirmed'),
    errors=meta.get('charge_errors',[]),
    return_seen='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
    units='battery current is OEM discharge-positive uA; FG raw mA have opposite sign')
(a.directory/'charge-assessment.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
