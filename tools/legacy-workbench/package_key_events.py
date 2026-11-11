"""Package the notification-only service with truthful RAM-only ADT policy."""
from pathlib import Path
import gzip,hashlib,json,re,struct,subprocess,sys,tomllib
from audit_boot_candidate import Fat
from avb_guard import check_image
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'UniXNU_elden/scripts'))
from darwinboot_preflight import adt_walk
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
out=ROOT/'edl_work/key-events-20261107';out.mkdir(exist_ok=True)
source=ROOT/'edl_work/chrono-wait-20261107/boot-signed.img'
assert check_image(source,'boot_a').sha256=='cb58921c537823d8b8a863539915425583876d124f8a68a66c13005c2b500078'
image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
fat=Fat(raw[0x400080:]);adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
assert sha(adt)=='a43f642b63d3da9d1ba2fc786bdae960dbb721b475f70cc173a977baa2142950'
def parse(data):
    nodes={};positions={}
    def walk(off,parent):
        start=off;np,nc=struct.unpack_from('<II',data,off);off+=8;props={}
        for _ in range(np):
            name=data[off:off+32].split(b'\0')[0].decode();size=struct.unpack_from('<I',data,off+32)[0]&0x7fffffff;off+=36
            assert name not in props and off+size<=len(data)
            props[name]=data[off:off+size];off+=(size+3)&~3
        path=parent+'/'+props['name'].split(b'\0')[0].decode();assert path not in nodes
        nodes[path]=props;positions[path]=(start,np,off,nc)
        for _ in range(nc):off=walk(off,path)
        return off
    assert walk(0,'')==len(data);return nodes,positions
old,positions=parse(adt);chosen='/device-tree/chosen';assert 'ephemeral-storage' not in old[chosen]
start,np,end,nc=positions[chosen]
prop=b'ephemeral-storage'.ljust(32,b'\0')+struct.pack('<II',4,1)
new=bytearray(adt[:end]+prop+adt[end:]);struct.pack_into('<I',new,start,np+1)
after,_=parse(new);assert after[chosen].pop('ephemeral-storage')==struct.pack('<I',1) and after==old
assert adt_walk(new)['window']==0x180000000
(out/'kernel.macho.adt').write_bytes(new)
kernel=(out/'kernel.macho').read_bytes();assert sha(kernel)==json.loads((out/'derivative.json').read_text())['derivative_sha256']
assert b'ELDEN_KEY_EVENTS registered' in kernel
menu=fat.read('/EFI/UniXNU/unixnu.toml').decode();base_args=tomllib.loads(menu)['entry'][0]['args']
menu=menu.replace(base_args,base_args+' elden-key-events=1',1);session=sha(kernel+new+menu.encode())[:16]
menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
cfg=tomllib.loads(menu);assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
assert 'elden-recovery-selftest' not in menu and all(len(e.get('args','').encode())<1024 for e in cfg['entry'])
(out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n')
payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
for local,target in [('kernel.macho','Payloads/kernel.macho'),('kernel.macho.adt','Payloads/kernel.macho.adt'),('unixnu.toml','unixnu.toml')]:
    subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/local),'::/EFI/UniXNU/'+target],check=True)
updated=Fat(payload.read_bytes());unchanged={}
for p in ('Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi'):
    before=fat.read('/EFI/UniXNU/'+p);assert updated.read('/EFI/UniXNU/'+p)==before;unchanged[p]=sha(before)
assert updated.read('/EFI/UniXNU/Payloads/kernel.macho.adt')==new
assert updated.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel
packed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0);header=bytearray(image[:4096]);struct.pack_into('<I',header,8,len(packed))
unsigned=bytes(header)+packed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
report=dict(session=session,kernel_sha256=sha(kernel),adt_sha256=sha(new),previous_adt_sha256=sha(adt),
    unchanged=unchanged,ephemeral_storage=1,all_existing_adt_properties_preserved=True,device_tested=False,
    only_functional_change='catalog AppleKeyStoreState notification service with RAM-only admission; all state clients disabled')
(out/'packet.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
