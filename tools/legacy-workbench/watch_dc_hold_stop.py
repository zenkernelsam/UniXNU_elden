"""One session-bound fastboot request during a freshly observed native DC hold.

The existing console owns the USB port and checks readiness. This watcher only
creates its control file. No retry, no device writes, no inference of success.
"""
from pathlib import Path
import argparse,datetime,json,re,time
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--session',required=True);p.add_argument('--seconds',type=int,default=600);a=p.parse_args()
assert re.fullmatch('[0-9a-f]{16}',a.session) and 0<a.seconds<=900
run=a.run.resolve();run.mkdir(parents=True,exist_ok=True)
control=run/'control.json';assert not control.exists()
started=time.monotonic();hold=None;offset=0;pending=b'';decision='not observed';last_phase=None
while time.monotonic()-started<a.seconds:
 files=list((run/'usb').glob('*.bin'))
 if len(files)>1:decision='multiple captures; suppressed';break
 if files:
  with files[0].open('rb') as f:f.seek(offset);data=f.read();offset+=len(data)
  lines=(pending+data).split(b'\n');pending=lines.pop()
  if len(pending)>16384:decision='oversized line; suppressed';break
  for line in lines:
   if b'panic(cpu' in line or b'ELDEN_RECOVERY PANIC_WAIT' in line:
    decision='panic before hold request; suppressed';break
   m=re.search(rb'ELDEN_DC_NATIVE phase=(\d+) error=(\d+) attempts=(\d+) completions=(\d+) low=(\d+)',line)
   if m:
    last_phase=int(m[1])
    if tuple(map(int,m.groups()))==(5,0,1,1,1) and hold is None:hold=time.monotonic()
    elif hold is not None and last_phase!=5:decision='hold ended before request; suppressed';break
  if decision.endswith('suppressed'):break
  if hold is not None and time.monotonic()-hold>=2:
   if last_phase!=5 or time.time()-files[0].stat().st_mtime>1.5:decision='hold log stale; suppressed';break
   request=dict(id='dc-hold-stop',session=a.session,command='fastboot')
   tmp=run/'control.pending.json';tmp.write_text(json.dumps(request)+'\n');tmp.replace(control)
   decision='requested fastboot during observed hold; device outcome requires log';break
 time.sleep(.05)
report=dict(session=a.session,decision=decision,bytes_seen=offset,last_phase=last_phase,elapsed_seconds=time.monotonic()-started,utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(run/'hold-stop-watcher.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
