"""Independent artifact audit: original executables, native PM/HID and stock AVB."""
import argparse,gzip,hashlib,json,struct,tomllib
from pathlib import Path
from avb_guard import check_image,metadata
from audit_boot_candidate import Fat,PORT
import sys
sys.path.insert(0,str(PORT/'scripts'));import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def raw(path):
    b=path.read_bytes();return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--attempt',type=int,default=3)
    a=p.parse_args();assert 2<=a.attempt<=9
    out=ROOT/('edl_work/battery-telemetry-20261108/final-v'+str(a.attempt));b=(out/'kernel.macho').read_bytes();parse_kernel(b)
    symbols=maker.symbols(b);plan=json.loads((out/'kernel.json').read_text())
    original=bytearray(b);offset=maker.locate(b,0xfffffff004aaecfc)[1]
    assert struct.unpack_from('<I',b,offset)[0]==0x17ffffd4;struct.pack_into('<I',original,offset,0xaa1503e0)
    for module,digest in plan['original_executable_sha256'].items():
        e=next(e for e in plan['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC')
        pos=maker.locate(b,e['va'])[1];assert sha(original[pos:pos+e['size']])==digest,module
    abi=json.loads((out/'hid-abi.json').read_text());assert abi['original_slots']==207 and abi['original_instance_size']==208
    for name,va in abi['bindings'].items():assert symbols[name]==va,name
    for name in ['_elden_battery_snapshot','_elden_battery_runtime_configure','_elden_battery_runtime_update',
                 '_elden_battery_runtime_unavailable','__ZTV12EldenBattery','__ZN15IOPMPowerSource12updateStatusEv',
                 '__ZN15IOPMPowerSource11setAmperageEi','_sheng_adsp_send_epoch','_elden_ucsi_next']:
        assert symbols.get(name,0)!=0,name
    assert b'BatteryDataValid' in b and b'EldenBatteryTelemetryUnavailable' in b
    before=raw(ROOT/'edl_work/hid-20261107/boot-signed.img');bf=Fat(before[0x400080:]);reports=[]
    for variant in ['off','query']:
        d=out/variant;admission=check_image(d/'boot-signed.img','boot_a')
        assert admission.index==1738713600 and admission.location==0 and admission.bytes==100663296
        assert metadata(d/'boot-signed.img')['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
        r=raw(d/'boot-signed.img');assert r[:0x400080]==before[:0x400080];f=Fat(r[0x400080:])
        assert f.read('/EFI/UniXNU/Payloads/kernel.macho')==b
        packet=json.loads((d/'packet.json').read_text());cfg=tomllib.loads(f.read('/EFI/UniXNU/unixnu.toml').decode())
        assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
        args=cfg['entry'][0]['args'].split()
        for key in ['elden-battery','elden-battery-profile','elden-battery-current']:
            assert [w for w in args if w.startswith(key+'=')]==[key+'=0']
        for key,value in [('elden-adsp',0 if variant=='off' else 2),('elden-pmic',0 if variant=='off' else 1),('elden-ucsi-query',0 if variant=='off' else 1)]:
            assert [w for w in args if w.startswith(key+'=')]==[key+'='+str(value)]
        assert packet['dsp_start_enabled']==(variant=='query') and not packet['charger_control_set_enabled']
        assert not packet['power_source_publication'] and 'elden-debug-session='+packet['session'] in args
        for path,digest in packet['unchanged'].items():
            data=f.read('/EFI/UniXNU/'+path);assert data==bf.read('/EFI/UniXNU/'+path) and sha(data)==digest
        assert adt_walk(f.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
        reports.append(dict(variant=variant,image_sha256=admission.sha256,session=packet['session'],index=admission.index,location=admission.location))
    result=dict(accepted=True,kernel_sha256=sha(b),images=reports,original_modules=len(plan['original_executable_sha256']),
        hid_slots=207,native_hid_bindings=len(abi['bindings']),native_power_source_linked=True,
        publication_requires_live_contract=True,hardware_tested=False)
    (out/'independent-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
