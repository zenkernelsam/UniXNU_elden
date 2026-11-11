"""Qualify board battery units and replay captured GLINK through the production C decoder."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
OUT=ROOT/'edl_work/battery-publication-20261109';OUT.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
source=DEVICE/'drivers/EldenPmicGlink'
frozen=ROOT/'research/basic-bundle-20261108/source-compiled-v11/power'
for name in ('ChargePoll.h','ChargePoll.c'):assert sha(source/name)==sha(frozen/name),'Captured field order changed: '+name
enum=(source/'ChargePoll.h').read_text().split('enum EldenChargeField {',1)[1].split('};',1)[0]
names=re.findall(r'\bEldenCharge\w+\b',enum);assert names.pop()=='EldenChargeFieldCount' and len(names)==45
capture=ROOT/'edl_work/charge-modes-input-fix-20261109'
files=list((capture/'usb').glob('*.bin'));assert len(files)==1
text=files[0].read_bytes().decode('utf-8','replace')
session='eced71442fde8dff';assert set(re.findall(r'ELDEN STATUS v1 session=([0-9a-f]{16})',text))=={session}
records={}
pattern=r'ELDEN_PMIC_RAW cycle=(\d+) epoch=(\d+) age-ms=(\d+) first=(\d+) valid=([0-9a-f]+) values=([0-9a-f,]+)'
for c,e,age,first,valid,words in re.findall(pattern,text):
    c,e,age,first=int(c),int(e),int(age),int(first);valid=int(valid,16)
    words=[int(v,16) for v in words.split(',')]
    assert first in (0,9,18,27,36) and len(words)==9
    row=records.setdefault((e,c),dict(epoch=e,cycle=c,age=age,valid=valid,words=[None]*45))
    assert (row['age'],row['valid'])==(age,valid)
    for i,v in enumerate(words,first):
        assert row['words'][i] in (None,v),'Conflicting duplicate cycle';row['words'][i]=v
complete=[r for _,r in sorted(records.items()) if None not in r['words']]
assert len(complete)>=30,'Require sustained actual hardware samples'
lines=[' '.join([str(r['epoch']),str(r['cycle']),str(r['age']),format(r['valid'],'x')]+[format(v,'08x') for v in r['words']]) for r in complete]
stdin='\n'.join(lines)+'\n';(OUT/'native-words.txt').write_text(stdin)
binary=OUT/'replay-battery';cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','gcc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',
 '-I'+wsl(source),wsl(source/'BatterySnapshot.c'),wsl(source/'ReplayBatterySnapshot.c'),'-o',wsl(binary)]
subprocess.run(cmd,check=True)
r=subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--',wsl(binary)],input=stdin,text=True,capture_output=True,check=True)
decoded=[json.loads(line) for line in r.stdout.splitlines()];assert len(decoded)==len(complete)
for raw,d in zip(complete,decoded):
    assert d['result']==0 and (d['epoch'],d['cycle'])==(raw['epoch'],raw['cycle'])
    v=dict(zip(names,raw['words']));current=v['EldenChargeCurrent'];current=current if current<2**31 else current-2**32
    assert d['ma']==int(-current/1000),'Apple current direction'
    assert d['mv']==v['EldenChargeVoltage']//1000 and d['percent']==(v['EldenChargeCapacity']+50)//100
    if not v['EldenChargeEnabled']:
        assert not d['charging'],'Battery hold is not charging'
        if d['external'] and not v['EldenChargeInputSuspended']:assert d['external_capable'],'Battery hold must retain the input'
    if current>0:assert not d['charging'],'Online plus discharge must not become charging'
admitted=[d for d in decoded if d['publication']==0]
assert len(admitted)>=50 and all(d['publication']!=0 for d in decoded[:3])
assert admitted[0]['cycle']==4,'Initial default and settling frames must be withheld'
assert not any(d['charging'] for d in admitted),'This capture has no qualified net-charge interval'
(OUT/'decoded-native.json').write_text(json.dumps(decoded,indent=2)+'\n')
oem_path=ROOT/'edl_work/charge-continuation-20261109/oem/power.json'
oem=json.loads(oem_path.read_text());assert oem['read_only'] and oem['product']=='TB322FC'
b=oem['supplies']['battery'];g=oem['supplies']['qcom-battery'];u=oem['supplies']['usb']
assert b['status']=='Charging' and int(b['current_now'])<0 and int(g['fg1_current'])>0 and int(g['fg2_current'])>0
assert u['online']=='1' and u['input_current_limit']=='500000'
module=ROOT/'edl_work/power-elden-20261107/stock-power-modules/qti_battery_charger.ko'
assert sha(module)=='4aeb25c04ab4fed445ba9457be498a811f90ea5a941283ae9679190c2f41b2b4'
proof=dict(accepted=True,profile=1,current_multiplier=-1,maximum_age_ms=10000,
    source_sha256={n:sha(source/n) for n in ('ChargePoll.h','ChargePoll.c','BatterySnapshot.h','BatterySnapshot.c','BatteryRuntime.cpp','ReplayBatterySnapshot.c')},
    stock_module_sha256=sha(module),oem_capture_sha256=sha(oem_path),
    native_capture_sha256=sha(files[0]),native_session=session,complete_cycles=len(complete),incomplete_cycles=len(records)-len(complete),
    current_ma_range=[min(d['ma'] for d in decoded),max(d['ma'] for d in decoded)],
    net_charging_cycles=sum(d['charging'] for d in decoded),
    qualified_publication_cycles=len(admitted),first_publication_cycle=admitted[0]['cycle'],
    qualified_net_charging_cycles=sum(d['charging'] for d in admitted),
    qualified_current_ma_range=[min(d['ma'] for d in admitted),max(d['ma'] for d in admitted)],
    raw_two_gauge_units_not_published=True,protect_hold_retains_external=True,
    production_c_sanitizers=['address','undefined'],native_iopmpowersource_tested=False,
    qualification='OEM units/signs and production decoder against captured native samples; UI publication still untested')
(OUT/'contract.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({k:v for k,v in proof.items() if k not in ('source_sha256','stock_module_sha256','oem_capture_sha256','native_capture_sha256')},indent=2))
if r.stderr:print(r.stderr,file=sys.stderr)
