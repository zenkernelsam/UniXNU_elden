"""Read an Elden USB capture; never turn a published service into a hardware pass."""
import argparse,hashlib,json,re
from pathlib import Path

def assess(data,session,mode):
    text=data.decode('utf-8','replace')
    sessions=set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))
    if sessions!={session}:raise ValueError('Missing or foreign USB debug session')
    def lines(prefix):return re.findall(re.escape(prefix)+r'[^\r\n]*',text)
    def fields(line):return dict(re.findall(r'([\w-]+)=(-?[0-9a-fA-F]+)\b',line))
    acquired=lines('ELDEN_BACKLIGHT acquire=');inspects=lines('ELDEN_BACKLIGHT inspect=')
    releases=lines('ELDEN_BACKLIGHT release=');roundtrips=lines('ELDEN_BACKLIGHT roundtrip=')
    published=lines('ELDEN_BACKLIGHT published AppleARMBacklight')
    errors=lines('ELDEN_BACKLIGHT refused')+lines('ELDEN_BACKLIGHT brightness failed=')
    errors+=re.findall(r'panic\(cpu[^\r\n]*',text)
    reasons=[]
    if len(acquired)!=1 or fields(acquired[0]).get('acquire')!='0' or fields(acquired[0]).get('stage')!='5':reasons.append('Controller ownership was not qualified')
    expected={'inspect':'0','mode':str(mode),'ready':'1','chip-writes':'0'}
    if len(inspects)!=1 or any(fields(inspects[0]).get(k)!=v for k,v in expected.items()):reasons.append('Chip/configuration read was not qualified')
    if errors:reasons.append('A driver refusal, transport failure or panic was recorded')
    if mode in (1,2):
        if len(releases)!=1 or fields(releases[0]).get('release')!='0' or fields(releases[0]).get('quarantine')!='0':reasons.append('Hardware ownership was not released')
    if mode==1 and (roundtrips or published or (releases and fields(releases[0]).get('chip-writes')!='0')):reasons.append('Chip-read profile contained a write or service activation')
    if mode==2:
        good=False
        if len(roundtrips)==1:
            f=fields(roundtrips[0])
            try:
                before,dim,restored,writes=(int(f[k]) for k in ('before','dim','restored','chip-writes'))
                good=f['roundtrip']=='0' and 1<=dim<before<=4095 and restored==before and writes>0
            except (KeyError,ValueError):pass
        if not good:reasons.append('A nontrivial dim-and-restore sequence was not confirmed')
    registered=mode==3 and len(published)==1 and not reasons
    if mode==3:reasons.append('Service registration alone does not qualify CoreBrightness or visible brightness')
    return dict(session=session,mode=mode,log_bytes=len(data),log_sha256=hashlib.sha256(data).hexdigest(),
        passed=not reasons,reasons=reasons,service_registered=registered,
        acquire=acquired,inspect=inspects,roundtrip=roundtrips,release=releases,
        requests=lines('ELDEN_BACKLIGHT request=')[-5:],errors=errors,
        dropped_max=max(map(int,re.findall(r'ELDEN STATUS[^\r\n]*dropped=(\d+)',text)),default=0),
        fastboot_ack=('ELDEN ACK FASTBOOT session='+session) in text,
        reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text,
        fastboot_enumeration_checked=False,device_operations=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--expect-session',required=True);p.add_argument('--mode',type=int,choices=(1,2,3),required=True)
    p.add_argument('--output',type=Path);a=p.parse_args()
    files=list((a.directory/'usb').glob('*.bin'))
    if len(files)!=1:raise SystemExit('Expected exactly one raw USB capture')
    r=assess(files[0].read_bytes(),a.expect_session,a.mode)
    if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(r,indent=2))
if __name__=='__main__':main()
