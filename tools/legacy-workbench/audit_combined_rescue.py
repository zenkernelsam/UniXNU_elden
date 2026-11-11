"""Independently audit the signed 56 MiB compressed XNU/recovery packet."""
import argparse
import gzip
import hashlib
import json
import lzma
from pathlib import Path
import struct
import tomllib

from audit_boot_candidate import Fat,ROOT,PORT,module,emulate_entry
from avb_guard import check_image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--fdt-mailbox-audit',type=Path,help='positive EFI mailbox and recovery contract evidence for the new loader')
p.add_argument('--stage2-manifest',type=Path,help='separate compiled applications manifest for optional stage2 entries')
p.add_argument('--memory-entry',action='store_true')
p.add_argument('--boot-metadata-audit',type=Path,help='current-boot properties contract for the new recovery loader')
a=p.parse_args()
admitted=check_image(a.image,'boot_a')
data=a.image.read_bytes()
assert admitted.bytes==96*1024*1024
assert data[:8]==b'ANDROID!' and struct.unpack_from('<I',data,40)[0]==4
assert struct.unpack_from('<I',data,12)[0]==0 and struct.unpack_from('<I',data,1580)[0]==0
size=struct.unpack_from('<I',data,8)[0]
raw=gzip.decompress(data[4096:4096+size])
assert len(raw)==128+4*1024*1024+56*1024*1024
assert raw[128:0x400080]==(ROOT/'edl_work/offline-20261105/old-fd.bin').read_bytes()
loader_sha='364b0d79a44a6f4cdf7167b79c8f071fb81e55e5ba14e4188ea64889c39b63b8'
if a.fdt_mailbox_audit:
    evidence=json.loads(a.fdt_mailbox_audit.read_text(encoding='utf-8'))
    assert all(evidence.get(key) is True for key in ('fdt_mailbox_contract','loadfile2_contract_passed',
        'error_return_cleanup_passed','fresh_retry_passed','invalid_pointer_rejected','oversize_FDT_rejected',
        'live_FDT_copy_and_restore_passed'))
    loader_sha=evidence['application_sha256']
    assert raw[0x400080+0x1c0:0x400080+0x1d8]==b'ELDFDT01'+struct.pack('<IIQ',1,0,0)
    assert hashlib.sha256(raw[:128]).hexdigest()=='207917264ae1edf87c9fed9ab7365c6d5e979070c41c599ee2359f79c6b5e968'
if a.boot_metadata_audit:
    metadata=json.loads(a.boot_metadata_audit.read_text(encoding='utf-8'))
    assert metadata['passed'] and metadata['current_slot_inherited'] and metadata['failure_cleanup_and_retry']
    assert metadata['full_original_cpio_bytes']==145697280
    assert metadata['application_sha256']==loader_sha
verify=module('combined_phase_verify',PORT/'scripts/verify_phase2_artifact.py')
verify.verify_embedded_shim(raw[:128],56*1024*1024)
kind,label=verify.verify_fat_type(raw[0x400080:])
assert label=='ELDENRAM'
fat=Fat(raw[0x400080:])
kernel=lzma.decompress(fat.read('/EFI/UniXNU/Payloads/kernel.macho.lzma'))
linux=lzma.decompress(fat.read('/EFI/UniXNU/Rescue/Image.efi.lzma'))
initrd=lzma.decompress(fat.read('/EFI/UniXNU/Rescue/recovery-initrd.cpio.lzma'))
adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
loader=fat.read('/EFI/UniXNU/EldenPayloadLoader.efi')
expected={
 'kernel':('3fd40e9f88bbcfdde801d9d5bfed9a9a836921393ff5bfed2c1058137c73e4b6',kernel),
 'linux':('55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05',linux),
 'initrd':('81230f5048bca16f96aa5acc89399f9646d82e1e9db04cf9c5f949397d9b595e',initrd),
 'adt':('b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3',adt),
 'loader':(loader_sha,loader)}
for name,(wanted,blob) in expected.items():
    assert hashlib.sha256(blob).hexdigest()==wanted,name+' identity differs'
stage2_assets={}
if a.stage2_manifest:
    assert a.fdt_mailbox_audit, 'stage2 requires current ABL FDT transport'
    built=json.loads(a.stage2_manifest.read_text(encoding='utf-8'))
    for name in ('EldenStage2.efi','EldenUfsProbe.efi'):
        blob=fat.read('/EFI/UniXNU/'+name)
        stage2_assets[name]=dict(bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest())
        assert stage2_assets[name]==built['applications'][name]
for image in (linux,loader):
    assert image[:2]==b'MZ'
    pe=struct.unpack_from('<I',image,60)[0]
    assert image[pe:pe+4]==b'PE\0\0'
    assert struct.unpack_from('<H',image,pe+4)[0]==0xaa64
    assert struct.unpack_from('<H',image,pe+24+68)[0]==10
assert initrd[:6]==b'070701' and initrd[-12:]==b'#BOOTCONFIG\n'
length,checksum=struct.unpack_from('<II',initrd,len(initrd)-20)
config=initrd[-20-length:-20]
assert sum(config)&0xffffffff==checksum
assert b'androidboot.mode=recovery\n' in config
assert b'androidboot.force_normal_boot=0\n' in config
menu=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
assert menu['boot']==dict(timeout=-1,fallback_timeout=600,default='reset')
entries={entry['id']:entry for entry in menu['entry']}
assert len(entries)==len(menu['entry'])==(11 if a.stage2_manifest else 8)+(1 if a.memory_entry else 0)
if a.memory_entry:
    assert entries['memory']['type']=='efi' and entries['memory']['path']=='EldenPayloadLoader.efi' and entries['memory']['args']=='memory'
if a.stage2_manifest:
    for name,path,args in (('ufs-stage2-inspect','EldenUfsProbe.efi','inspect'),
                           ('stage2-current','EldenStage2.efi','current'),
                           ('stage2-previous','EldenStage2.efi','previous')):
        item=entries[name]
        assert item['type']=='efi' and item['path']==path and item['args']==args and 'initrd' not in item
for name,probe in (('xnu',10),('inspect',2),('smmu',7),('banks',8)):
    item=entries[name]
    assert item['type']=='efi' and item['path']=='EldenPayloadLoader.efi'
    assert item['args'].startswith('xnu ') and f'native-ufs-probe={probe}' in item['args'].split()
    assert 'native-ufs-lun=4' in item['args'].split() and 'initrd' not in item
item=entries['twrp']
assert item['type']=='efi' and item['path']=='EldenPayloadLoader.efi' and 'initrd' not in item
assert item['args'].startswith('linux ') and 'bootconfig' in item['args'].split()
assert 'rdinit=/init' in item['args'].split()
assert all(len(item.get('args','').encode('utf-16-le'))+2<=4096 for item in entries.values())
preflight=module('combined_preflight',PORT/'scripts/darwinboot_preflight.py')
maker=module('combined_probe',PORT/'scripts/make_entry_probe_kernel.py')
assert preflight.adt_walk(adt)['window']==0x10000000
report=dict(image_sha256=admitted.sha256,index=admitted.index,location=admitted.location,
    bytes=admitted.bytes,payload_bytes=56*1024*1024,FD_unchanged=True,fat_type=kind,
    assets={name:dict(bytes=len(blob),sha256=sha) for name,(sha,blob) in expected.items()},
    menu_entries=[dict(id=entry['id'],title=entry['title'],type=entry['type']) for entry in menu['entry']],
    complete_vendor_TWRP_initrd=True,bootconfig_checksum_valid=True,
    entry_fdt_mailbox=bool(a.fdt_mailbox_audit),
    optional_stage2_assets=stage2_assets,
    entry_simulation=emulate_entry(kernel,preflight,maker),hardware_tested=False,TWRP_boot_tested=False)
a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({key:value for key,value in report.items() if key not in ('assets','menu_entries')},indent=2))
