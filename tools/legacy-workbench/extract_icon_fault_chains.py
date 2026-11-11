"""Extract complete atomic fault rows and their exact per-process cache slide."""
from pathlib import Path
import argparse,hashlib,json,re
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--session',required=True);a=p.parse_args();assert re.fullmatch('[0-9a-f]{16}',a.session)
logs=list((a.run/'usb').glob('*.bin'));assert len(logs)==1
raw=logs[0].read_bytes();meta=json.loads(logs[0].with_suffix('.json').read_text());assert meta['candidate_session']==a.session
text=raw.decode(errors='replace').replace('\r','')
text=re.sub(r'\nELDEN STATUS v1 session='+re.escape(a.session)+r'(?: [a-z_]+=[a-z0-9-]+)+\n','',text)
pattern=re.compile(r'^ELDEN_ICON_CHAIN pid=(\d+) fp=([0-9a-f]+) count=(\d+) reason=(\d+) read-error=(-?\d+) frames=([0-9a-f,]*)$',re.M)
samples=[]
for match in pattern.finditer(text):
 pid=int(match[1]);frames=[int(x,16) for x in match[6].split(',') if x];assert len(frames)==int(match[3])<=16
 assert all(4096<=x<(1<<39) and not x&3 for x in frames)
 prefix=text[:match.start()]
 slides=list(re.finditer(r'ELDEN_FAULT_META pid='+str(pid)+r' pc=0 slide=([0-9a-f]+) ',prefix))
 faults=list(re.finditer(r'ELDEN_FAULT pid='+str(pid)+r' name=iconservicesagent esr=([0-9a-f]+) pc=0 lr=([0-9a-f]+) far=0',prefix))
 if not slides or not faults:continue
 samples.append({'fault_index':len(samples),'name':'iconservicesagent','pid':pid,'pc':0,'lr':int(faults[-1][2],16),'slide':int(slides[-1][1],16),'fp':int(match[2],16),'frames':frames,'reason':int(match[4]),'read_error':int(match[5]),'source':'synchronous current EL0 state matched to PC/LR; user frame chain is not atomic against other threads'})
out=r/'research/icon-fault-trace-20261111/live-v89';out.mkdir(parents=True,exist_ok=True)
(out/'samples.json').write_text(json.dumps(samples,indent=2)+'\n')
(out/'capture-binding.json').write_text(json.dumps({'raw_file':str(logs[0]),'snapshot_bytes':len(raw),'snapshot_sha256':hashlib.sha256(raw).hexdigest(),'session':a.session,'boot_nonce':meta['boot_nonce'],'capture_terminal':bool(meta.get('raw_sha256')),'complete_samples':len(samples)},indent=2)+'\n')
print(json.dumps({'samples':len(samples),'depths':[len(s['frames']) for s in samples],'reasons':[s['reason'] for s in samples]}))
