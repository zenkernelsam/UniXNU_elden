"""Export bounded saved contexts for exact-cache, read-only symbolication."""
from pathlib import Path
import argparse,re,json,hashlib
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--run',type=Path,required=True);a=p.parse_args();assert 71<=a.version<=99
run=a.run.resolve();assert run.is_relative_to(r/'edl_work')
out=r/f'research/ui-waits-v{a.version}-20261111';out.mkdir(exist_ok=True)
paths=list((run/'usb').glob('*.bin'));assert len(paths)==1;b=paths[0].read_bytes();text=b.decode(errors='replace')
pat=r'ELDEN_WAIT pass=(\d+) name=(\S+) pid=(\d+) thread=(\d+) slide=([0-9a-f]+) state=([0-9a-f]+) wait=([0-9a-f]+) cont=([0-9a-f]+) trap=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) frames=([0-9a-f,]*) error=(\d+)'
samples=[]
for m in re.finditer(pat,text):
 sample=dict(name=m[2],pid=int(m[3]),thread=int(m[4]),slide=int(m[5],16),pc=int(m[10],16),lr=int(m[11],16),frames=[int(x,16) for x in m[12].split(',') if x],error=int(m[13]));sample['pass']=int(m[1]);samples.append(sample)
(out/'samples.json').write_text(json.dumps(samples,indent=2))
(out/'input.json').write_text(json.dumps(dict(raw_log=str(paths[0]),snapshot_bytes=len(b),snapshot_sha256=hashlib.sha256(b).hexdigest(),complete_capture=False),indent=2))
sym=(r/'research/touch-event-waits-20261110/symbolicate.py').read_text().replace('research/touch-event-waits-20261110',f'research/ui-waits-v{a.version}-20261111').replace("('SpringBoard','backboardd','Preferences')","('iconservicesagent','SpringBoard','Preferences','backboardd')")
(out/'symbolicate.py').write_text(sym);print('Prepared',len(samples),'samples')
