"""Check a completed UCSI notification experiment, without device I/O."""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text());assert meta['raw_sha256']==hashlib.sha256(raw).hexdigest()
flash=json.loads((a.run/'flash/result.json').read_text());assert flash['success'] and flash['image_sha256']==flash['full_readback_sha256']
packet=json.loads((Path(flash['image']).parent/'packet.json').read_text());assert packet['session']==meta['candidate_session']
flags=packet['profiles'][packet['default']];assert flags['elden-ucsi-init']==2
qualification=json.loads((Path(flash['image']).parent/'ucsi-events-qualification.json').read_text())
expected_mask=f"{int(qualification['notification_control'],16)>>16:x}"
text=raw.decode('ascii',errors='replace')
rows=[dict(ready=int(r),mask=m,acks=int(n),pending=int(c),notifications=int(v)) for r,m,n,c,v in re.findall(r'ELDEN_UCSI_EVENTS ready=(\d+) mask=([0-9a-f]+) connector-acks=(\d+) pending=(\d+) notifications=(\d+)',text)]
assert rows and all(r['ready']==1 and r['mask']==expected_mask and r['pending']<=2 for r in rows)
errors=re.findall(r'ELDEN_UCSI stopped[^\r\n]*',text)
pd_mv=sorted(set(map(int,re.findall(r'ELDEN_PD_CONTRACT port=2 result=0[^\r\n]*requested-mv=(\d+)',text))))
pulses=re.findall(r'ELDEN_PD_PULSE low[^\r\n]*',text)
no_pulse=not flags.get('elden-pd-bootstrap',0)
if no_pulse:assert not pulses and not re.search(r'ELDEN_USB_ICL request=0\b',text)
report=dict(session=packet['session'],expected_mask=expected_mask,image_sha256=flash['image_sha256'],log_sha256=meta['raw_sha256'],bytes=len(raw),
    dropped=max([int(s['dropped']) for s in meta.get('statuses',[])],default=0),cycles_reported=len(rows),
    event_acks_last=rows[-1]['acks'],pending_last=rows[-1]['pending'],notification_count=rows[-1]['notifications'],
    all_ready=True,errors=errors,pd_voltages_mv=pd_mv,no_routing_pulse=no_pulse,pulses=pulses,
    reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC' in text,firmware_internal_ready_flag_directly_read=False,
    interpretation='Firmware code and successful full-notification/ACK protocol support OPM-ready; power/fast-charge completion needs separate evidence.')
(a.run/'ucsi-events-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
