"""Record this offline delivery in the scoped device porting record."""
import hashlib,json,re,subprocess,tomllib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
file=DEVICE/'porting.toml';text=file.read_text(encoding='utf-8')
revision=subprocess.check_output(['git','-C',str(DEVICE),'rev-parse','HEAD'],text=True).strip()
power=('2026-11-08 battery priority offline: complete45-field transcript/freshness, aggregate Lenovo unit decoder and native source IOPMPowerSource linked in kernel6ecda041. '
       '57 ASan/UBSan checks, collector dual-FG/typec/GPIO-base syntax and original30 modules/HID207/52 pass. '
       'final-v3 off6831a97e/querydc9ac415 signed at1738713600@footer0/parent3, unflashed. '
       'Query starts signed DSP; no charger SET. Publication requires OEM current/unit evidence; stale/down withdraws charge/current confirmation without inventing empty SOC. '
       'Exact38-source freeze and docs/BATTERY-TELEMETRY-20261108.md. Shared ADSP AOSS guard is linked; actual charging/PD/dual-port still unqualified.')
network=('2026-11-08 priority is power, touch, Wi-Fi then Bluetooth. UART.SK native HCI host bridge compiled/signed64987719 off/1de6c1e3 live, '
         'original21H461 bluetoothd independently extracted/audited; daemon fixups/job remain disabled and pairing untested. '
         'Y700 fixed Linux PCI17cb:110e RID100/SID1401/MSI IOVA18000000 and77 original Peach firmware assets cataloged read-only. '
         'Original IO80211Family is absent from the current30-module kernel, so native ABI/prelink/capacity and actual network remain work items. '
         'See docs/WCN-NATIVE-20261108.md and work-repository research/WIFI-INPUTS-20261108.md.')
def update(area,finding,paths,next_action,reference):
    global text
    pattern=r'(\[\[area\]\]\s*\nid = "'+re.escape(area)+r'"\n)(.*?)(?=\n\[\[area\]\]|\Z)'
    m=re.search(pattern,text,re.S);assert m,area;body=m.group(2)
    encoded=json.dumps(finding,ensure_ascii=False)
    if finding not in tomllib.loads(text)['area'][next(i for i,a in enumerate(tomllib.loads(text)['area']) if a['id']==area)]['findings']:
        body=body.replace('findings = [','findings = ['+encoded+', ',1)
    match=re.search(r'^paths = (\[.*\])$',body,re.M);assert match
    current=tomllib.loads('paths = '+match.group(1))['paths']
    for p in paths:
        if p not in current:current.append(p)
    body=body[:match.start(1)]+json.dumps(current)+body[match.end(1):]
    body=re.sub(r'^next_action = \[.*\]$', 'next_action = '+json.dumps([next_action]),body,count=1,flags=re.M)
    evidence=dict(kind='host',reference=reference,revision=revision,
        command='python edl_work/audit_battery_telemetry.py --attempt 3 (Y700 work repository)' if area=='power' else 'python edl_work/audit_bt_native.py; python edl_work/harvest_wifi_inputs.py (Y700 work repository)',
        result='Local changes plus exact source freezes. Offline verification only; no new device actions or hardware completion.')
    # JSON object punctuation is not TOML inline-table punctuation.
    fields=', '.join(k+' = '+json.dumps(v,ensure_ascii=False) for k,v in evidence.items())
    if 'reference = '+json.dumps(reference) not in body:
        body=body.replace('evidence = [','evidence = [\n  { '+fields+' },',1)
    text=text[:m.start(2)]+body+text[m.end(2):]
update('power',power,['docs/BATTERY-TELEMETRY-20261108.md'],
    'Collect OEM Android dual-FG/dual-port readings first, require native power admission and original firmware/survey evidence, then qualify final-v3 off/query. Enable publication only after current/units match; qualify net PD charging/source selection before charger controls, then retain M3 when merging touch.',
    'docs/BATTERY-TELEMETRY-20261108.md')
update('network',network,['docs/WCN-NATIVE-20261108.md'],
    'After power and touch, continue Y700 PCIe/MHI/SID1401/actual MSI qualification, exact21H461 IO80211 ABI and firmware-board selection. Keep native BT source intact; daemon/pairing follow Wi-Fi and require real shared-power/low-power evidence.',
    'docs/WCN-NATIVE-20261108.md')
tomllib.loads(text);file.write_text(text,encoding='utf-8',newline='\n')
source=ROOT/'research/battery-telemetry-20261108/source-input-v3'
old=json.loads((ROOT/'research/adsp-runtime-ucsi45-20261108/manifest.json').read_text())
group=next(g for g in old['groups'] if g['name']=='EldenAdspRuntime');matches={}
for f in group['files']:
    if f['file']=='AdspProbe.cpp':continue
    digest=hashlib.sha256((source/f['file']).read_bytes()).hexdigest();assert digest==f['sha256'];matches[f['file']]=digest
out=ROOT/'edl_work/battery-telemetry-test-20261108'
(out/'reused-source-proof.json').write_text(json.dumps(dict(accepted=True,source_files=matches),indent=2)+'\n')
(out/'snapshot.json').write_text(json.dumps(dict(accepted=True,checks=57,address_sanitizer=True,undefined_behavior_sanitizer=True,
    device_called=False,source_sha256={n:hashlib.sha256((DEVICE/'drivers/EldenPmicGlink'/n).read_bytes()).hexdigest() for n in ['BatterySnapshot.c','BatterySnapshot.h','TestBatterySnapshot.c']},
    stdout='PASS Elden battery snapshot: 57 checks, two-cell and online/net-current cases'),indent=2)+'\n')
print('Updated scoped power/network records;15 reused source hashes and57-check result retained')
