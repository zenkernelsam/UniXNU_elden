"""Assess a completed fixed GPIO/input read; never grants control permission."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys

p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
subprocess.run([sys.executable,str(Path(__file__).with_name('assess_power_path.py')),str(a.run)],check=True,capture_output=True)
power=json.loads((a.run/'power-path-assessment.json').read_text())
logs=list((a.run/'usb').glob('*.bin'));assert len(logs)==1
raw=logs[0].read_bytes();assert hashlib.sha256(raw).hexdigest()==power['raw_sha256']
text=raw.decode('ascii',errors='replace')
heads=re.findall(r'ELDEN_GPIO_PATH cycle=(\d+) suspend=([0-9a-f]{2});',text)
rows=re.findall(r'ELDEN_GPIO_REG index=(\d+) base=([0-9a-f]{4}) type=([0-9a-f]{2}) subtype=([0-9a-f]{2}) mode=([0-9a-f]{2}) vin=([0-9a-f]{2}) source=([0-9a-f]{2}) enable=([0-9a-f]{2}) status=([0-9a-f]{2})',text)
assert len(power['samples'])==1 and len(heads)==1 and len(rows)==2,'Require one complete deferred snapshot'
assert int(heads[0][0])==power['samples'][0]['cycle'] and not power['errors']
pins=[]
for expected,row in zip([9,10],rows):
    index=int(row[0]);values=list(map(lambda x:int(x,16),row[1:]))
    assert index==expected and values[0]==0x8800+index*0x100
    base,kind,subtype,mode,vin,source,enable,status=values
    assert kind==0x10 and subtype==0x12,'Unexpected GPIO identity'
    enabled=bool(enable&0x80)
    level=((source>>7)&1) if (mode&3)==1 and not (source&15) else None
    pins.append(dict(index=index,base=base,type=kind,subtype=subtype,mode=mode,vin=vin,source=source,enable=enable,status=status,
                     output_level=level,enabled=enabled,equals_stock=values[1:]==[0x10,0x12,1,1,0,0x80,0x80]))
report=dict(session=power['session'],log_sha256=power['raw_sha256'],bytes=power['bytes'],dropped=power['dropped'],
            power=power['samples'][0],usb_suspend_request=int(heads[0][1],16),gpios=pins,
            snapshot_complete=True,reset_after_sync=power['reset_after_sync'],reverse_blocking_qualified=False,
            interpretation='GPIO state and USB suspend are observations, not permission to override the ADSP owner or change current budgets.')
(a.run/'gpio-path-assessment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
