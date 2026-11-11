"""Package the new diagnostic kernel over the device-proven root-match inputs."""
from pathlib import Path
import gzip,hashlib,json,re,struct,subprocess,sys,tomllib
from audit_boot_candidate import Fat
from avb_guard import check_image
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'UniXNU_elden/scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
out=ROOT/'edl_work/spmi-probe-20261107'
source=ROOT/'edl_work/watchdog-auto-20261107/boot-signed.img'
assert check_image(source,'boot_a').sha256=='5a35288a8761e1bc76f288be927b8e312b01e91e3aa527adaaf41f656f0c3343'
image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
fat=Fat(raw[0x400080:]);kernel=(out/'kernel.macho').read_bytes()
assert sha(kernel)=='f1a7cc06a4f462d2ffae0031752cbbcd06e046047d762be733601c66b2dc72e5'
assert struct.unpack_from('<I',kernel,maker.locate(kernel,0xfffffff004aaecfc)[1])[0]==0x17ffffd4
assert b'ELDEN_DATA preflight target=%d spec=%d' in kernel and b'ELDEN_USB request present=%u enabled=%u' in kernel
symbols=maker.symbols(kernel);assert all(n in symbols for n in ('_native_ram_block_volume_seed','__ZN14NativeRamBlock5startEP9IOService','_native_verified_block_root_authorized'))
assert b'E5D06647-CCB4-491D-A897-3C21B55703CF' in kernel
parsed=parse_kernel(kernel);old=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
menu=fat.read('/EFI/UniXNU/unixnu.toml').decode()
menu=menu.replace('\ntimeout = 120\n','\ntimeout = 8\n',1).replace('default = "shutdown"','default = "xnu"',1)
first=menu.index('id = "xnu"');at=menu.index('args = "-v ',first)
menu=menu[:at]+menu[at:].replace('args = "-v ','args = "-v elden-reboot-probe=1 ',1)
assert b'ELDEN_RECOVERY' not in kernel
config=tomllib.loads(menu)
assert config['boot']['default']=='xnu' and config['boot']['timeout']==8
assert 'elden-disable-watchdogd=1' in next(e for e in config['entry'] if e['id']=='xnu')['args']
assert all(len(e.get('args',''))<1024 for e in config['entry'])
session=sha(kernel+menu.encode())[:16]
menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
(out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n')
payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
for src,dst in [('kernel.macho','Payloads/kernel.macho'),('unixnu.toml','unixnu.toml')]:
 subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/src),'::/EFI/UniXNU/'+dst],check=True)
f=Fat(payload.read_bytes());unchanged={}
for p in ('Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi'):
 a=fat.read('/EFI/UniXNU/'+p);b=f.read('/EFI/UniXNU/'+p);assert a==b;unchanged[p]=sha(b)
assert f.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel
assert adt_walk(f.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
newraw=raw[:0x400080]+payload.read_bytes();compressed=gzip.compress(newraw,mtime=0)
header=bytearray(image[:4096]);struct.pack_into('<I',header,8,len(compressed))
unsigned=bytes(header)+compressed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
r=dict(session=session,kernel_sha256=sha(kernel),unsigned_sha256=sha(unsigned),unchanged=unchanged,
       only_functional_change='read-only PMIC arbiter version and reboot-reason channel ownership probe',device_tested=False)
(out/'packet.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');print(json.dumps(r,indent=2))
