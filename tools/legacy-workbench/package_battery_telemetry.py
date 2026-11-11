"""Package an independently selectable battery build over the qualified HID baseline.

This command only builds local files. Publishing requires a recorded OEM unit/
current comparison; query itself starts signed DSP firmware, and does no charger SET.
"""
from pathlib import Path
import argparse,gzip,hashlib,json,re,shutil,struct,subprocess,sys,tomllib
from avb_guard import check_image
from audit_boot_candidate import Fat,PORT
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def wsl(path):return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant',choices=['off','query','publish'],required=True)
    p.add_argument('--qualified-contract',type=Path)
    p.add_argument('--attempt',type=int,default=3)
    a=p.parse_args();multiplier=0;qualification=None
    assert 2<=a.attempt<=9
    if a.variant=='publish':
        if not a.qualified_contract:p.error('publish requires an OEM/PMIC comparison file')
        qualification=json.loads(a.qualified_contract.read_text())
        assert qualification['accepted'] is True and qualification['profile']==1
        multiplier=qualification['current_multiplier'];assert multiplier in (-1,1)
        assert qualification['oem_comparison'] and qualification['pmic_comparison']
        for key in ('oem_comparison','pmic_comparison'):
            item=qualification[key];f=Path(item['path']).resolve()
            assert f.is_relative_to(ROOT/'edl_work') and sha(f.read_bytes())==item['sha256']
    base=ROOT/'edl_work/hid-20261107';source=base/'boot-signed.img'
    assert check_image(source,'boot_a').sha256=='7282b272395a3c7a9a099bad3ab1f10b20a6951320129a96373f3410a868a363'
    compiled=ROOT/('edl_work/battery-telemetry-20261108/final-v'+str(a.attempt));out=compiled/a.variant;out.mkdir(exist_ok=True)
    image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
    before=Fat(raw[0x400080:]);kernel=(compiled/'kernel.macho').read_bytes()
    assert sha(kernel)==json.loads((compiled/'derivative.json').read_text())['derivative_sha256']
    symbols=maker.symbols(kernel)
    for name in ['_native_ufs_part_find','_native_ufs_part_read','_sheng_adsp_probe','_elden_pmic_client_start',
                 '_elden_battery_snapshot','_elden_battery_runtime_update','__ZTV12EldenBattery']:
        assert name in symbols,name
    for text in [b'ELDEN_FIRMWARE_CHECK',b'ELDEN_ADSP stage=',b'ELDEN_PMIC_RAW cycle=',
                 b'ELDEN_BATTERY refused unqualified',b'ELDEN_RECOVERY PANIC_WAIT',b'ELDEN ACK HOME']:
        assert text in kernel,text
    parse_kernel(kernel)
    menu=before.read('/EFI/UniXNU/unixnu.toml').decode();old=tomllib.loads(menu);args=old['entry'][0]['args']
    keys=('elden-adsp','elden-pmic','elden-modem-fw-check','elden-ucsi-query','elden-battery',
          'elden-battery-profile','elden-battery-current')
    assert not any(re.search(r'\b'+key+r'=',args) for key in keys)
    if a.variant=='off':flags='elden-modem-fw-check=0 elden-adsp=0 elden-pmic=0 elden-ucsi-query=0 elden-battery=0 elden-battery-profile=0 elden-battery-current=0'
    else:flags='elden-modem-fw-check=0 elden-adsp=2 elden-pmic=1 elden-ucsi-query=1 elden-battery=%d elden-battery-profile=%d elden-battery-current=%d'%(
        a.variant=='publish',a.variant=='publish',multiplier)
    updated=args+' '+flags;assert len(updated.encode())<1024
    menu=menu.replace(args,updated,1)
    session=sha(kernel+menu.encode())[:16];menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
    assert tomllib.loads(menu)['boot']==old['boot'] and old['boot']['default']=='xnu' and old['boot']['timeout']==8
    (out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n');(out/'kernel.macho').write_bytes(kernel)
    payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
    for filename,destination in [('kernel.macho','Payloads/kernel.macho'),('unixnu.toml','unixnu.toml')]:
        subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/filename),'::/EFI/UniXNU/'+destination],check=True)
    fat=Fat(payload.read_bytes());unchanged={}
    for filename in ['Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi']:
        data=fat.read('/EFI/UniXNU/'+filename);assert data==before.read('/EFI/UniXNU/'+filename);unchanged[filename]=sha(data)
    assert fat.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel
    assert adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
    compressed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0)
    header=bytearray(image[:4096]);struct.pack_into('<I',header,8,len(compressed))
    unsigned=bytes(header)+compressed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
    shutil.copyfile(compiled/'hid-abi.json',out/'hid-abi.json')
    shutil.copyfile(compiled/'kernel.json',out/'source-plan.json')
    packet=dict(variant=a.variant,flags=flags,session=session,kernel_sha256=sha(kernel),unsigned_sha256=sha(unsigned),
        unchanged=unchanged,dsp_start_enabled=a.variant!='off',charger_control_set_enabled=False,
        power_source_publication=a.variant=='publish',current_multiplier=multiplier,device_tested=False,
        qualification=qualification,test_gate='native power admission, OEM comparison, original firmware and ADSP survey qualification')
    (out/'packet.json').write_text(json.dumps(packet,indent=2)+'\n',encoding='utf-8');print(json.dumps(packet,indent=2))
if __name__=='__main__':main()
