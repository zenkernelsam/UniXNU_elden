from pathlib import Path
import re,json
r=Path(__file__).resolve().parents[1];out=r/'research/icon-service-v71-20261111';out.mkdir(exist_ok=True)
src=(r/'research/icon-overlay-20261111/extract_system_symbols.py').read_text().replace('research/icon-system-symbols-20261111','research/icon-service-v71-20261111')
src=src.replace('names=[]','names=[];export_addresses={}')
src=src.replace('if term:names.append(prefix)','if term:\n     names.append(prefix);flags,q=uleb(p)\n     if not flags&8:\n      address,q=uleb(q);export_addresses[prefix]=hex(address if flags&3==2 else va+address)')
src=src.replace("walk(0,'',set());","walk(0,'',set());(out/(short+'-export-addresses.json')).write_text(json.dumps(export_addresses,indent=2));")
(out/'extract.py').write_text(src)
b=next((r/'edl_work/live-icon-progress-v71-20261111/usb').glob('*.bin')).read_bytes().decode(errors='replace');samples=[]
pat=r'ELDEN_WAIT pass=(\d+) name=(\S+) pid=(\d+) thread=(\d+) slide=([0-9a-f]+) state=([0-9a-f]+) wait=([0-9a-f]+) cont=([0-9a-f]+) trap=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) frames=([0-9a-f,]*) error=(\d+)'
for m in re.finditer(pat,b):
 if m[2]!='iconservicesagent':continue
 sample=dict(name=m[2],pid=int(m[3]),thread=int(m[4]),slide=int(m[5],16),pc=int(m[10],16),lr=int(m[11],16),frames=[int(x,16) for x in m[12].split(',') if x],error=int(m[13]));sample['pass']=int(m[1]);samples.append(sample)
(out/'samples.json').write_text(json.dumps(samples,indent=2))
sym=(r/'research/touch-event-waits-20261110/symbolicate.py').read_text().replace('research/touch-event-waits-20261110','research/icon-service-v71-20261111').replace("('SpringBoard','backboardd','Preferences')","('iconservicesagent',)")
(out/'symbolicate.py').write_text(sym);print('Prepared',len(samples),'samples and address export')
