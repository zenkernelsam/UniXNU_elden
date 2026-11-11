"""Read a completed, SHA-bound power-path register capture; never authorize writes."""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
files=list((a.run/'usb').glob('*.bin'));assert len(files)==1
raw=files[0].read_bytes();meta=json.loads(files[0].with_suffix('.json').read_text())
assert meta.get('raw_sha256')==hashlib.sha256(raw).hexdigest()
flash=json.loads((a.run/'flash/result.json').read_text());assert flash['success'] and flash['full_readback_sha256']==flash['image_sha256']
packet=json.loads((Path(flash['image']).parent/'packet.json').read_text());assert packet['session']==meta['candidate_session']
text=raw.decode('utf-8','replace');rows=[]
pattern=r'ELDEN_POWER_PATH cycle=(\d+) epoch=(\d+) path=([0-9a-f]+)/([0-9a-f]+) usb50=(\d+) wls50=(\d+) stable=(\d+) selection=(\d+); GET-only bus0 SID7'
for c,e,before,after,usb,wls,stable,selection in re.findall(pattern,text):
    b,v=int(before,16),int(after,16);u,w=int(usb),int(wls)
    assert int(stable)==int(((b^v)&0x30)==0) and int(selection)==((v>>4)&3)
    rows.append(dict(cycle=int(c),epoch=int(e),path_before=b,path_after=v,usb_raw=u,wls_raw=w,usb_ma=u*50 if u<=100 else None,wls_ma=w*50 if w<=100 else None,stable=bool(int(stable)),selection=int(selection)))
errors=re.findall(r'ELDEN_POWER_REG (?:stopped|rejected)[^\r\n]*',text)
report=dict(session=meta['candidate_session'],bytes=len(raw),raw_sha256=meta['raw_sha256'],image_sha256=flash['image_sha256'],samples=rows,
    distinct_states=sorted(set((r['path_after'],r['usb_raw'],r['wls_raw']) for r in rows)),errors=errors,
    dropped=max([int(x['dropped']) for x in meta.get('statuses',[])],default=0),reconnections=meta.get('reconnections',[]),
    reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
    interpretation='Selection bits and register units come from the actual stock firmware. This is read-only observation, not reverse-current protection or higher-current authorization.')
(a.run/'power-path-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
