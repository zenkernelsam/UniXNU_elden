"""Summarize completed, hash-bound native PD experiments without device I/O."""
import argparse,hashlib,json,re
from pathlib import Path

def assess(run):
    metas=list((run/'usb').glob('*.json'));assert len(metas)==1
    meta=json.loads(metas[0].read_text());raw=metas[0].with_suffix('.bin').read_bytes()
    assert meta.get('raw_sha256')==hashlib.sha256(raw).hexdigest(),'Capture is incomplete or changed'
    flash=json.loads((run/'flash/result.json').read_text());assert flash['success'] and flash['full_readback_sha256']==flash['image_sha256']
    image=Path(flash['image']);packet=json.loads((image.parent/'packet.json').read_text())
    assert packet['session']==meta['candidate_session']
    mode=packet['pd_bootstrap_mode'];assert mode in (1,2)
    text=raw.decode('utf-8',errors='replace').replace('\r','');lines=text.splitlines()
    phases=[];gates=[];samples=[];pending=None
    for line in lines:
        m=re.fullmatch(r'ELDEN_PD_BOOT phase=(\d+) error=(\d+) pulse=(\d+) high=(\d+) cycle=(\d+) deadline=(\d+) suspend=(\d+) restored=(\d+)',line)
        if m:phases.append(dict(zip(['phase','error','pulse','high','cycle','deadline','suspend','restored'],map(int,m.groups()))))
        m=re.search(r'ELDEN_PD_GATE cycle=(\d+) phase=(\d+).*?pending=(\d+) icl=(\d+)/(\d+)',line)
        if m:gates.append(dict(zip(['cycle','phase','pending','applied','observed'],map(int,m.groups()))))
        m=re.fullmatch(r'ELDEN_PMIC cycle=(\d+) epoch=(\d+) valid=([0-9a-f]+) usb-icl-sets=(\d+) voltage-raw=(\d+) current-raw=(-?\d+) temperature-raw=(-?\d+)',line)
        if m:
            pending=dict(cycle=int(m[1]),epoch=int(m[2]),valid=int(m[3],16),voltage_uv=int(m[5]),current_ua=int(m[6]),temperature=int(m[7]))
        m=re.fullmatch(r'ELDEN_PMIC usb1=(\d+) usb2=(\d+) suspend=(\d+) charging-enable=(\d+) pps=(\d+); fg1-v=(\d+) fg2-v=(\d+) fg1-i=(-?\d+) fg2-i=(-?\d+)',line)
        if m and pending:
            pending.update(fg1_ma=int(m[8]),fg2_ma=int(m[9]));samples.append(pending);pending=None
    completed=next((x for x in phases if x['phase']==6 and x['error']==0 and x['restored']==1),None)
    # An ACK or state-machine transition alone does not prove the physical hold.
    zero_seen=False;interrupted=False
    for g in gates:
        if g['phase']!=4:continue
        if not g['pending'] and g['applied']==0 and g['observed']==0:zero_seen=True
        elif zero_seen and not g['pending'] and g['observed']!=0:interrupted=True
    after=[s for s in samples if s['valid']==(1<<45)-1 and completed and s['cycle']>=completed['cycle']+6]
    tail=after[-10:]
    for s in tail:s['coherent_net_charge']=s['current_ua']<0 and s['fg1_ma']>0 and s['fg2_ma']>0
    pulses=re.findall(r'ELDEN_PD_PULSE low result=(-?\d+) elapsed-us=(\d+) held=(\d+)',text)
    pulse_ok=len(pulses)==1 and int(pulses[0][0])==0 and int(pulses[0][1])<=1000000 and int(pulses[0][2])==0
    held=mode==1 or (zero_seen and not interrupted)
    statuses=meta.get('statuses',[])
    return dict(mode=mode,session=meta['candidate_session'],image_sha256=flash['image_sha256'],
        log_sha256=meta['raw_sha256'],log_bytes=len(raw),dropped=int(statuses[-1]['dropped']) if statuses else None,
        boot_nonce=meta.get('boot_nonce'),reconnections=meta.get('reconnections',[]),phases=phases,
        pulse=pulses,pulse_restored=pulse_ok,pause_zero_observed=zero_seen,pause_readback_interrupted=interrupted,
        controller_completed=bool(completed) and pulse_ok and held,
        stopped_cleanly='ELDEN_PD_BOOT stop=1;' in text,reset_after_sync='ELDEN_RPC RESET_AFTER_SYNC' in text,
        pd_9v_contract='requested-mv=9000 requested-ma=2220' in text,
        last_stable_samples=tail,all_last_ten_net_charging=len(tail)==10 and all(s['coherent_net_charge'] for s in tail),
        mean_last_current_ua=sum(s['current_ua'] for s in tail)//len(tail) if tail else None,
        interpretation='Negative aggregate plus positive dual FG supports net charge in sampled conditions. This does not meter individual ports, prove peak PD power, or complete the device goal.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
    result=assess(a.run);(a.run/'pd-assessment.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('last_stable_samples','phases')},indent=2))
if __name__=='__main__':main()
