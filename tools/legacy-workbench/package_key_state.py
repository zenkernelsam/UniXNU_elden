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
out=ROOT/'edl_work/key-state-20261107'
source=ROOT/'edl_work/key-events-recheck-20261107/boot-signed.img'
assert check_image(source,'boot_a').sha256=='b35296fb188cf830a727c2a121a2a67c856e45b645004f18abd7aba8a4a53dd6'
image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
fat=Fat(raw[0x400080:]);kernel=(out/'kernel.macho').read_bytes()
assert sha(kernel)==json.loads((out/'derivative.json').read_text())['derivative_sha256']
assert struct.unpack_from('<I',kernel,maker.locate(kernel,0xfffffff004aaecfc)[1])[0]==0x17ffffd4
assert b'ELDEN_DATA preflight target=%d spec=%d' in kernel and b'ELDEN_USB request present=%u enabled=%u' in kernel
assert b'ELDEN_UFS final completion recheck' in kernel
assert b'ELDEN_FAULT_META' in kernel and b'ELDEN_DISPLAY registered' in kernel and b'ELDEN_RECOVERY PANIC_WAIT' in kernel
parsed=parse_kernel(kernel);old=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
menu=fat.read('/EFI/UniXNU/unixnu.toml').decode()
assert len(tomllib.loads(menu)['entry'][0]['args'].encode())<1024
menu=menu.replace(old['entry'][0]['args'],old['entry'][0]['args']+' elden-key-state=1',1)
assert len(tomllib.loads(menu)['entry'][0]['args'].encode())<1024
session=sha(kernel+menu.encode())[:16]
menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
assert tomllib.loads(menu)['boot']==old['boot'] and old['boot']['default']=='xnu' and old['boot']['timeout']==8
assert 'elden-recovery-selftest' not in menu
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
       only_functional_change='opt-in upstream keybag state for verified unprotected RAM; no key operations; profile/context wait capture',device_tested=False)
(out/'packet.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');print(json.dumps(r,indent=2))
