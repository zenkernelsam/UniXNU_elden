"""Check actual sent USB limits against the matching captured BMS types."""
from pathlib import Path
import argparse,json,hashlib,re
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text());digest=hashlib.sha256(raw).hexdigest();assert digest==meta['raw_sha256']
flash=json.loads((a.run/'flash/result.json').read_text());packet=json.loads((Path(flash['image']).parent/'packet.json').read_text())
assert flash['success'] and flash['image_sha256']==flash['full_readback_sha256'] and packet['default'] in ('pd-handoff','pd-handoff-quiet','dc-noop-quiet') and packet['usb_sdp_only']
assert packet['session']==meta['candidate_session'];text=raw.decode('utf-8','replace');cycles={}
for m in re.finditer(r'ELDEN_PMIC_RAW cycle=(\d+) epoch=(\d+) age-ms=(\d+) first=(\d+) valid=([0-9a-f]+) values=([0-9a-f,]+)',text):
 row=cycles.setdefault(int(m[1]),{});row.update({int(m[4])+i:int(v,16) for i,v in enumerate(m[6].split(','))})
sends=[]
for value,budget,previous,cycle,guard in re.findall(r'ELDEN_USB_ICL request=(\d+) budget=(\d+) previous-get=(\d+) cycle=(\d+); SDP-guard=(\d+)',text):
 row=cycles[int(cycle)];assert len(row)==45 and int(guard)==1
 sends.append(dict(value=int(value),budget=int(budget),previous=int(previous),cycle=int(cycle),adapter=row[20],real=row[43]))
 assert row[20]==1 and row[43]==1 and int(value)<=500000 and int(budget)<=500000
held=[dict(cycle=int(c),fresh=int(f),adapter=int(a),real=int(r),observed=int(v)) for c,f,a,r,v in re.findall(r'ELDEN_USB_HANDOFF deferred cycle=(\d+) fresh=(\d+) adapter=(\d+) real=(\d+) get=(\d+)',text)]
mixed=[x for x in held if x['cycle']>=4 and (x['adapter']!=1 or x['real']!=1)]
dropped=max([int(x['dropped']) for x in meta.get('statuses',[])],default=0)
report=dict(guard_exercised=bool(sends and mixed),all_observed_sent_limits_had_sdp_identity=True,complete_log=dropped==0 and not meta.get('discontinuities'),sends=sends,deferrals=held,mixed_or_pd_deferrals=mixed,
 log_sha256=digest,bytes=len(raw),session=packet['session'],image_sha256=flash['image_sha256'],dropped=dropped,
 reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC' in text,
 pd_voltages_mv=sorted(set(map(int,re.findall(r'ELDEN_PD_CONTRACT port=2 result=0[^\r\n]*requested-mv=(\d+)',text)))),
 bootstrap=re.findall(r'ELDEN_PD_BOOT[^\r\n]*',text),higher_current_programmed=False,
 interpretation='Typed command guard evidence; PD voltage, source retention and net charging require their own observations.')
(a.run/'usb-handoff-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
