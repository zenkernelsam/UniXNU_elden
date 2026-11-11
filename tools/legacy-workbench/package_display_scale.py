"""Supply the missing explicit UI scale, preserving the tested kernel and disk inputs."""
from pathlib import Path
import gzip,hashlib,json,re,struct,subprocess,sys,tomllib
from audit_boot_candidate import Fat
from avb_guard import check_image
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'UniXNU_elden/scripts'))
from darwinboot_preflight import adt_walk
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
out=ROOT/'edl_work/display-scale-20261107';out.mkdir(exist_ok=True)
source=ROOT/'edl_work/display-r3-20261107/boot-signed.img'
assert check_image(source,'boot_a').sha256=='2fca34744cda460c80a7dd3ac84a61e2db7f150c1ce3e05d099132e60b6ac23c'
image=source.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
fat=Fat(raw[0x400080:]);adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
assert sha(adt)=='70096378c9699115dc8a2b5666ac80677026a53c919767573273e174cd1d3c83'
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
old,positions=parse(adt);chosen='/device-tree/chosen';assert 'display-scale' not in old[chosen]
start,np,end,nc=positions[chosen]
prop=b'display-scale'.ljust(32,b'\0')+struct.pack('<II',4,2)
new=bytearray(adt[:end]+prop+adt[end:]);struct.pack_into('<I',new,start,np+1)
after,_=parse(new);assert after[chosen].pop('display-scale')==struct.pack('<I',2) and after==old
assert adt_walk(new)['window']==0x180000000
(out/'kernel.macho.adt').write_bytes(new)
kernel=fat.read('/EFI/UniXNU/Payloads/kernel.macho');assert sha(kernel)=='cb888c2ade928cffb32ab2e990cab33fb932dbe35bb6ce8357f2ab5cb582ddc1'
menu=fat.read('/EFI/UniXNU/unixnu.toml').decode();session=sha(kernel+new+menu.encode())[:16]
menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
cfg=tomllib.loads(menu);assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
assert 'elden-recovery-selftest' not in menu and all(len(e.get('args','').encode())<1024 for e in cfg['entry'])
(out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n')
payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
for local,target in [('kernel.macho.adt','Payloads/kernel.macho.adt'),('unixnu.toml','unixnu.toml')]:
    subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/local),'::/EFI/UniXNU/'+target],check=True)
updated=Fat(payload.read_bytes());unchanged={}
for p in ('Payloads/kernel.macho','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi'):
    before=fat.read('/EFI/UniXNU/'+p);assert updated.read('/EFI/UniXNU/'+p)==before;unchanged[p]=sha(before)
assert updated.read('/EFI/UniXNU/Payloads/kernel.macho.adt')==new
packed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0);header=bytearray(image[:4096]);struct.pack_into('<I',header,8,len(packed))
unsigned=bytes(header)+packed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
report=dict(session=session,kernel_sha256=sha(kernel),adt_sha256=sha(new),previous_adt_sha256=sha(adt),
    unchanged=unchanged,ui_scale=2,all_existing_adt_properties_preserved=True,device_tested=False,
    only_functional_change='chosen/display-scale=2, explicit UI points policy; actual 1904x3040 scanout unchanged')
(out/'packet.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
