"""Independent signed dual-container, frozen runtime and complete rescue audit."""
import hashlib
import json
import lzma
from pathlib import Path
import struct
import tomllib
from avb_guard import check_image,metadata
from build_kshim_rescue48 import parse
from audit_boot_candidate import Fat,PORT,module

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'edl_work/prerequisites-20261106/kshim48'
image=OUT/'boot-signed.img';admitted=check_image(image,'boot_a');blob=image.read_bytes()
assert admitted.bytes==96*1024*1024 and admitted.index==1738713600 and admitted.location==0
assert metadata(image)['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
assert blob[:8]==b'ANDROID!' and struct.unpack_from('<I',blob,40)[0]==4 and struct.unpack_from('<I',blob,12)[0]==0
kernel=blob[4096:4096+struct.unpack_from('<I',blob,8)[0]];manifest=parse(kernel)
assert manifest['default']==0 and manifest['timeout']==10000 and len(manifest['entries'])==3
android=manifest['entries'][0];free=next(e for e in manifest['entries'] if e['type']==2);shim=next(e for e in manifest['entries'] if e['type']==3)
assert android['type']==1 and android['flags']==5 and android['offset']==0
restored=bytearray(kernel[:android['bytes']])
for offset,value in zip((0,16,32,40,48),manifest['base']):struct.pack_into('<Q',restored,offset,value)
assert hashlib.sha256(restored).hexdigest()=='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05'
assert hashlib.sha256(kernel[shim['offset']:shim['offset']+shim['bytes']]).hexdigest()=='8c1e18d419a50969fcf718e3585494ddba28cfe82a8a956f22ff666dafe66bf3'
assert free['load']==0xc2400000 and free['entry']==0 and free['flags']==6 and free['copy_max']==0x4000000
assert free['bytes']==128+(4<<20)+(48<<20) and free['bytes']<=free['copy_max']
for entry in manifest['entries']:assert entry['offset']+entry['bytes']<=manifest['manifest_offset']
payload=kernel[free['offset']:free['offset']+free['bytes']]
assert payload[128:0x400080]==(ROOT/'edl_work/offline-20261105/old-fd.bin').read_bytes()
assert hashlib.sha256(payload[128:0x400080]).hexdigest()=='e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534'
verify=module('rescue48_phase_verify',PORT/'scripts/verify_phase2_artifact.py')
verify.verify_embedded_shim(payload[:128],48<<20)
fat_bytes=payload[0x400080:];verify.verify_fat_type(fat_bytes)
assert fat_bytes[0x1c0:0x1d8]==b'ELDFDT01'+struct.pack('<IIQ',1,0,0)
fat=Fat(fat_bytes);config=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
assert config['boot']['default']=='reset' and len(config['entry'])==7
entries={e['id']:e for e in config['entry']}
assert entries['current']['args']=='current' and entries['previous']['args']=='previous'
assert entries['twrp']['args'].startswith('linux ') and entries['memory']['args']=='memory'
assert all(e['type']!='xnu' for e in config['entry'])
proof=json.loads((ROOT/'edl_work/qemu-prereq-metadata-20261106/audit.json').read_text())
assert proof['passed'] and proof['memory_snapshot_mode_passed']
assert hashlib.sha256(fat.read('/EFI/UniXNU/EldenPayloadLoader.efi')).hexdigest()==proof['application_sha256']
assert hashlib.sha256(lzma.decompress(fat.read('/EFI/UniXNU/Rescue/Image.efi.lzma'))).hexdigest()=='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05'
assert hashlib.sha256(lzma.decompress(fat.read('/EFI/UniXNU/Rescue/recovery-initrd.cpio.lzma'))).hexdigest()=='81230f5048bca16f96aa5acc89399f9646d82e1e9db04cf9c5f949397d9b595e'
control=json.loads((OUT/'pack-audit.json').read_text());assert control['control_byte_identical']
report=dict(image_sha256=admitted.sha256,bytes=admitted.bytes,index=admitted.index,location=admitted.location,
    frozen_dual11_runtime=True,original_Android_kernel_reconstructed=True,control_rebuild_byte_identical=True,
    packed_bytes=len(kernel),FAT_bytes=48<<20,copy_limit=free['copy_max'],payload_bytes=len(payload),
    complete_recovery_preserved=True,inner_menu_entries=7,embedded_XNU=False,
    hardware_tested=False,limitation='KShimC chain, display/input, recovery and external UFS require owner device validation')
(OUT/'independent-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
