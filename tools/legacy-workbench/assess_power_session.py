"""Summarize a recorded Elden power session without accessing the tablet."""
import argparse, hashlib, json, re, statistics
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--expect-session',required=True)
a=p.parse_args()
files=sorted((a.directory/'usb').glob('*.bin'))
if len(files)!=1:raise SystemExit('Expected exactly one raw capture in this run')
data=files[0].read_bytes();text=data.decode('utf-8','replace')
statuses=re.findall(r'ELDEN STATUS[^\r\n]*',text)
sessions=set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))
if sessions!={a.expect_session}:raise SystemExit('Missing or foreign debug session')
pattern=r'ELDEN_PMIC cycle=(\d+) epoch=(\d+) valid=([0-9a-f]+) (?:writes=\d+|usb-icl-sets=\d+) voltage-raw=(\d+) current-raw=(-?\d+) temperature-raw=(-?\d+)'
samples=[dict(cycle=int(c),epoch=int(e),valid=v,voltage_uv=int(mv),current_ua=int(ma),temperature_raw=int(t))
         for c,e,v,mv,ma,t in re.findall(pattern,text)]
tail=samples[-10:]
inputs=re.findall(r'ELDEN_POWER_INPUTS[^\r\n]*',text)
def lines(prefix):return re.findall(re.escape(prefix)+r'[^\r\n]*',text)
r=dict(session=a.expect_session,log=str(files[0]),log_bytes=len(data),log_sha256=hashlib.sha256(data).hexdigest(),
       pmic_cycles=len(samples),last_samples=tail,
       tail_median_current_ua=statistics.median(s['current_ua'] for s in tail) if tail else None,
       last_inputs=inputs[-3:],ucsi_cycles=len(lines('ELDEN_UCSI cycle=')),
       ucsi_errors=lines('ELDEN_UCSI stopped'),notify=lines('ELDEN_PMIC_NOTIFY'),
       usb_budgets=lines('ELDEN_USB_BUDGET'),usb_icl=lines('ELDEN_USB_ICL'),
       vote_channel=lines('ELDEN_VOTE channel='),vote_errors=lines('ELDEN_VOTE stopped'),
       last_votes=lines('ELDEN_VOTE cycle=')[-4:],
       battery_ready=lines('ELDEN_BATTERY ready'),
       battery_held=lines('ELDEN_BATTERY held')[-5:],
       battery_published=lines('ELDEN_BATTERY published')[-5:],
       battery_refused=lines('ELDEN_BATTERY refused'),
       adc_controller=lines('ELDEN_ADC SPMI'),adc_snapshot=lines('ELDEN_ADC snapshot'),
       adc_samples=lines('ELDEN_ADC sample')[-4:],adc_end=lines('ELDEN_ADC end'),
       usb_thermal_shadow=lines('ELDEN_USB_THERMAL shadow')[-4:],
       thermal_gpio=lines('ELDEN_THERMAL GPIO'),thermal_samples=lines('ELDEN_THERMAL sample')[-4:],
       thermal_policy=lines('ELDEN_THERMAL policy')[-4:],thermal_faults=lines('ELDEN_THERMAL fail-closed'),
       thermal_stop=lines('ELDEN_THERMAL RPC stop='),thermal_end=lines('ELDEN_THERMAL end'),
       pd_status=lines('ELDEN_PD port=')[-2:],pd_source_pdos=lines('ELDEN_PD_SOURCE')[-14:],
       pd_contracts=lines('ELDEN_PD_CONTRACT')[-2:],
       last_ports=lines('ELDEN_UCSI port=')[-2:],last_status=statuses[-1:] or None,
       dropped_max=max(map(int,re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)),default=0),
       fastboot_ack=('ELDEN ACK FASTBOOT session='+a.expect_session) in text,
       reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
       charging_qualified=False)
(a.directory/'power-assessment.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in r.items() if k not in ('last_samples','last_ports','log_sha256')},indent=2))
