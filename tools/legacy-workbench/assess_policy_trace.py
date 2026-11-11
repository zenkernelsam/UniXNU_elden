"""Assess a completed fixed-category firmware capture without device access."""
from pathlib import Path
import argparse,json,hashlib,re
from decode_charger_ulog import decode
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text());digest=hashlib.sha256(raw).hexdigest();assert digest==meta['raw_sha256']
flash=json.loads((a.run/'flash/result.json').read_text());assert flash['success'] and flash['image_sha256']==flash['full_readback_sha256']
packet=json.loads((Path(flash['image']).parent/'packet.json').read_text());assert packet['default'] in ('pd-policy-trace','pd-handoff') and packet['session']==meta['candidate_session']
text=raw.decode('utf-8','replace');pages=decode(raw,[g['offset'] for g in meta.get('discontinuities',[])])
assert len(pages)<=12 and all(p['opcode']==0x18 for p in pages)
stages=re.findall(r'ELDEN_ULOG_FILTER[^\r\n]*',text)
configured='ELDEN_ULOG_FILTER phase=2 error=0 requests=1 acks=1 restored=0' in text
restored='ELDEN_ULOG_FILTER phase=4 error=0 requests=2 acks=2 restored=1' in text
q=re.findall(r'ELDEN_ULOG_FILTER queued phase=(\d+) mask=([0-9a-f]+)/([0-9a-f]+) level=(\d+)',text)
assert len(q)<=2
proper=q==[('1','4','2000020','3'),('3','0','0','4')]
fw='\n'.join(p['payload'].split(b'\0')[0].decode('utf-8','replace') for p in pages)
timeline=re.findall(r'ELDEN_ULOG_TRACE page=(\d+) uptime-ms=(\d+)',text)
assert len(timeline)<=12
times=[int(t) for _,t in timeline];assert all(y-x>=5000 for x,y in zip(times,times[1:]))
dropped=max([int(s['dropped']) for s in meta.get('statuses',[])],default=0)
report=dict(accepted=configured and restored and proper and len(pages)==12 and dropped==0 and not meta.get('discontinuities') and 'ELDEN_RPC RESET_AFTER_SYNC' in text,
 session=packet['session'],image_sha256=flash['image_sha256'],log_sha256=digest,bytes=len(raw),dropped=dropped,
 configured=configured,restored=restored,pages=len(pages),empty_pages=sum(not p['payload'][0] for p in pages),stages=stages,
 page_times_ms=times,icm_messages=sum(bool(re.search(r'ICM|icm_|scp_(?:svs|inov)_',line)) for line in fw.splitlines()),dpm_messages=fw.count('DPM'),reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC' in text,
 pd_voltages_mv=sorted(set(map(int,re.findall(r'ELDEN_PD_CONTRACT port=2 result=0[^\r\n]*requested-mv=(\d+)',text)))),higher_current_tested=False,
 interpretation='Diagnostic filter/trace qualification only; evaluate firmware messages separately for charging conclusions.')
(a.run/'policy-trace-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
