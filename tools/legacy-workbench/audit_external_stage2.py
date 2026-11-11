"""Independent FAT/component/config checks of the actual external bundle."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import tomllib
from audit_boot_candidate import Fat

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('bundle',type=Path)
p.add_argument('--menu-evidence',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--boot-metadata-audit',type=Path)
a=p.parse_args();raw=a.bundle.read_bytes();sha=hashlib.sha256(raw).hexdigest()
assert len(raw)==56*1024*1024 and raw[0x1c0:0x1d8]==bytes(24)
fat=Fat(raw)
expected={
 '/EFI/BOOT/BOOTAA64.EFI':'faa98d766a279a2057121833bd3636f339ba774f6c578363234807d42b7a498e',
 '/EFI/UniXNU/EldenPayloadLoader.efi':'2800abf84d4ada46df22370e015f4cbd75f679b0cf603e09d33653f1dbcac7df',
 '/EFI/UniXNU/Payloads/kernel.macho.adt':'b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3'}
if a.boot_metadata_audit:
    proof=json.loads(a.boot_metadata_audit.read_text(encoding='utf-8'))
    assert proof['passed'] and proof['current_slot_inherited'] and proof['failure_cleanup_and_retry']
    expected['/EFI/UniXNU/EldenPayloadLoader.efi']=proof['application_sha256']
for path,wanted in expected.items():assert hashlib.sha256(fat.read(path)).hexdigest()==wanted,path
decoded={
 '/EFI/UniXNU/Payloads/kernel.macho.lzma':'3fd40e9f88bbcfdde801d9d5bfed9a9a836921393ff5bfed2c1058137c73e4b6',
 '/EFI/UniXNU/Rescue/Image.efi.lzma':'55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05',
 '/EFI/UniXNU/Rescue/recovery-initrd.cpio.lzma':'81230f5048bca16f96aa5acc89399f9646d82e1e9db04cf9c5f949397d9b595e'}
for path,wanted in decoded.items():assert hashlib.sha256(lzma.decompress(fat.read(path))).hexdigest()==wanted,path
menu=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
assert menu['boot']==dict(timeout=-1,fallback_timeout=600,default='safe',connect_all=False)
assert menu['kits']==dict(autoload=False) and not menu.get('driver')
entries={i['id']:i for i in menu['entry']}
assert len(entries)==len(menu['entry'])==9
assert entries['safe']==dict(id='safe',title='Restart fixed rescue menu',type='action',action='exit')
for name,probe in [('xnu',10),('inspect',2),('smmu',7),('banks',8)]:
    entry=entries[name]
    assert entry['type']=='efi' and entry['path']=='EldenPayloadLoader.efi'
    assert entry['args'].startswith('xnu ') and f'native-ufs-probe={probe}' in entry['args'].split()
assert entries['twrp']['args'].startswith('linux ') and 'bootconfig' in entries['twrp']['args'].split()
evidence=json.loads(a.menu_evidence.read_text())
assert evidence['passed'] and evidence['return_via_cold_reset'] and evidence['bundle_sha256']==sha
assert not evidence.get('QEMU_ADT_substituted',False)
report=dict(bundle_sha256=sha,bytes=len(raw),accepted_UniXNU_unchanged=True,components_sha256=expected,
            decoded_payload_sha256=decoded,menu_entries=9,uses_current_EFI_FDT=True,
            actual_menu_and_cold_reset_tested=True,hardware_tested=False,UFS_transport_tested=False,
            external_DarwinBoot='uses the fixed firmware loader protocol; replacement needs explicit protocol selection')
a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('components_sha256','decoded_payload_sha256')},indent=2))
