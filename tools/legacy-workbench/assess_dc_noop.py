"""Bind the completed one-write DC result to its packet, raw capture and flash."""
from pathlib import Path
import argparse,hashlib,json,re
from dc_noop_qualification import verify
p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text());digest=hashlib.sha256(raw).hexdigest()
assert meta['raw_sha256']==digest
flash=json.loads((a.run/'flash/result.json').read_text());assert flash['success'] and flash['image_sha256']==flash['full_readback_sha256']
directory=Path(flash['image']).parent;packet=json.loads((directory/'packet.json').read_text());assert packet['default'] in ('dc-noop','dc-noop-quiet')
version=int(directory.parents[1].name[1:]);verify(root,version,directory)
session=packet['session'];assert session==meta['candidate_session']
text=raw.decode('utf-8','replace');sessions=set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text));assert sessions=={session}
stages=re.findall(r'ELDEN_DC_NOOP[^\r\n]*',text)
starts=[x for x in stages if 'start epoch=' in x];pre=[x for x in stages if 'pre-read accepted' in x]
writes=[x for x in stages if 'write queued' in x];verified=[x for x in stages if 'VERIFIED epoch=' in x]
terminal=re.findall(r'ELDEN_DC_NOOP terminal phase=(\d+) error=(\d+) attempted=(\d+) ack=(\d+) verified=(\d+)',text)
assert len(starts)<=1 and len(pre)<=1 and len(writes)<=1 and len(verified)<=1 and len(terminal)<=1
accepted=bool(len(starts)==len(pre)==len(writes)==len(verified)==1 and terminal==[('4','0','1','1','1')])
if accepted:assert text.index(starts[0])<text.index(pre[0])<text.index(writes[0])<text.index(verified[0])
dropped=max([int(x) for x in re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)],default=0)
stopped='ELDEN_DC_NOOP stop=1' in text
reset='ELDEN_RPC RESET_AFTER_SYNC' in text
health=[s for s in meta.get('statuses',[]) if s.get('cache')=='0' and s.get('data')=='0' and s.get('init_exec')=='1' and int(s.get('user_trap','0'))>0]
report=dict(accepted=accepted and stopped and reset and dropped==0 and bool(health),
 same_value_verified=accepted,session=session,image_sha256=flash['image_sha256'],log_sha256=digest,bytes=len(raw),
 dropped=dropped,discontinuities=meta.get('discontinuities',[]),stages=stages,terminal=terminal,
 healthy_root_data_pid1=bool(health),stop_confirmed=stopped,reset_after_sync=reset,
 higher_current_tested=False,dual_port_backfeed_protection_fully_verified=False,
 interpretation=('Fixed 500mA same-value write completed; higher-current and full power-path safety remain unqualified.' if accepted else
 'The same-value write was not verified. Read the terminal outcome; no higher-current or full power-path qualification is implied.'))
if report['discontinuities']:report['accepted']=False
(a.run/'dc-noop-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
