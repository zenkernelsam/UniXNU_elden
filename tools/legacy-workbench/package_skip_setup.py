"""Replace only the volatile Data seed with its verified official skip-setup copy."""
from pathlib import Path
import gzip,hashlib,json,re,struct,subprocess,tomllib
from audit_boot_candidate import Fat
from avb_guard import check_image
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'edl_work/skip-setup-20261107'
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
source=ROOT/'edl_work/display-preview-20261107/boot-signed.img'
assert check_image(source,'boot_a').sha256=='091273ce3ba9d76bacd9d3f3936266161fdcddc7fa774bf748eac1bf3a8f419c'
image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
fat=Fat(raw[0x400080:]);seed=(out/'data.packed').read_bytes();audit=json.loads((out/'audit.json').read_text())
assert sha(seed)==audit['packed_sha256']=='483125a27280f17d91de594f951cee4216f4888b764525b74db0f61ed62d34b3'
assert audit['expanded_bytes']==536870912 and audit['after']['uuid']=='E5D06647-CCB4-491D-A897-3C21B55703CF'
assert sha(fat.read('/EFI/UniXNU/Payloads/data.packed'))==audit['base_sha256']=='eb99e7370cfab4c3d58d97555632989b8f445a012a42f2f2acaab9189d5f8ba5'
menu=fat.read('/EFI/UniXNU/unixnu.toml').decode();session=sha(seed+menu.encode())[:16]
menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
cfg=tomllib.loads(menu);assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
assert 'elden-frame-preview=1' in cfg['entry'][0]['args'] and 'elden-recovery-selftest' not in menu
(out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n')
payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
for local,target in [('data.packed','Payloads/data.packed'),('unixnu.toml','unixnu.toml')]:
    subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/local),'::/EFI/UniXNU/'+target],check=True)
new=Fat(payload.read_bytes());unchanged={}
for p in ('Payloads/kernel.macho','Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','EldenPayloadLoader.efi'):
    original=fat.read('/EFI/UniXNU/'+p);assert new.read('/EFI/UniXNU/'+p)==original;unchanged[p]=sha(original)
assert new.read('/EFI/UniXNU/Payloads/data.packed')==seed
packed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0);header=bytearray(image[:4096]);struct.pack_into('<I',header,8,len(packed))
unsigned=bytes(header)+packed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
record=dict(session=session,kernel_sha256=unchanged['Payloads/kernel.macho'],data_packed_sha256=sha(seed),unchanged=unchanged,
    logical_data_bytes=536870912,volume_uuid=audit['after']['uuid'],only_functional_change='official skip-setup/no-auto-dim preferences in a fresh volatile Data copy',device_tested=False)
(out/'packet.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
