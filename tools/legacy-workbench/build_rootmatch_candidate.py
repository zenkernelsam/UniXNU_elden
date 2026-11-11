"""Offline ADT root-selection correction for the exact accepted authblob image."""
from pathlib import Path
import copy,gzip,hashlib,json,plistlib,re,struct,subprocess,sys,tomllib
from audit_boot_candidate import Fat
from audit_startup_candidates import adt_properties
from avb_guard import check_image

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'edl_work/authblob-20261107/startup-authblob-signed.img'
OUT=ROOT/'edl_work/rootmatch-20261107'
UUID='04769D26-2CA0-4825-B443-E2E5F88300F2'
EXPECTED='c264290c4f0fe31c43f76b6ef56dbeb8c64c76747ca74b9fc00a239433a3e153'

def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]

def main():
    admission=check_image(SOURCE,'boot_a');assert admission.sha256==EXPECTED
    image=SOURCE.read_bytes();raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
    assert len(raw)==0x400080+56*1024*1024
    fat=Fat(raw[0x400080:]);prefix='/EFI/UniXNU/'
    adt=fat.read(prefix+'Payloads/kernel.macho.adt');nodes=adt_properties(adt)
    chosen=nodes['/device-tree/chosen'];assert 'root-matching' not in chosen and 'boot-uuid' not in chosen
    blob=(ROOT/'edl_work/root-hash-20261107/044-76040-038.payload.bin').read_bytes()
    assert chosen['system-volume-auth-blob']==blob and len(blob)==208
    xml=('<dict><key>IOProviderClass</key><string>IOMedia</string>'
         '<key>IOPropertyMatch</key><dict><key>UUID</key><string>'+UUID+'</string></dict></dict>\0').encode()
    assert plistlib.loads(b'<plist>'+xml.rstrip(b'\0')+b'</plist>')=={'IOProviderClass':'IOMedia','IOPropertyMatch':{'UUID':UUID}}
    def transform(at,parent):
        start=at;n,c=struct.unpack_from('<II',adt,at);at+=8;name=None
        for _ in range(n):
            key=adt[at:at+32].split(b'\0')[0];size=struct.unpack_from('<I',adt,at+32)[0]&0x7fffffff
            if key==b'name':name=adt[at+36:at+36+size].split(b'\0')[0].decode()
            at+=36+((size+3)&~3)
        path=parent+'/'+name;props=adt[start+8:at]
        if path=='/device-tree/chosen':
            props+=struct.pack('<32sI',b'root-matching',len(xml))+xml+bytes(-len(xml)%4);n+=1
        children=[]
        for _ in range(c):child,at=transform(at,path);children.append(child)
        return struct.pack('<II',n,c)+props+b''.join(children),at
    updated,end=transform(0,'');assert end==len(adt)
    check=adt_properties(updated);assert check['/device-tree/chosen'].pop('root-matching')==xml and check==nodes
    old_menu=fat.read(prefix+'unixnu.toml').decode();before=tomllib.loads(old_menu)
    needle=' rd=uuid boot-uuid='+UUID
    assert old_menu.count(needle)==sum(e['type']=='xnu' for e in before['entry'])
    menu=old_menu.replace(needle,'')
    session=sha(updated+menu.encode())[:16]
    menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
    after=tomllib.loads(menu);expected=copy.deepcopy(before)
    for entry in expected['entry']:
        if entry['type']=='xnu':
            entry['args']=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,entry['args'].replace(needle,''))
    assert after==expected
    OUT.mkdir(exist_ok=True)
    (OUT/'kernel.macho.adt').write_bytes(updated);(OUT/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n')
    payload=OUT/'payload.img';payload.write_bytes(raw[0x400080:])
    for src,dst in [('kernel.macho.adt','Payloads/kernel.macho.adt'),('unixnu.toml','unixnu.toml')]:
        subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(OUT/src),'::'+prefix+dst],check=True)
    newfat=Fat(payload.read_bytes());unchanged={}
    def audit_dir(cluster,path):
        old=fat.directory(cluster)
        for name,(child,size,flags) in old.items():
            if name in ('.','..'):continue
            current=path+'/'+name
            if flags&0x10:audit_dir(child,current)
            else:
                a=fat.read(current);b=newfat.read(current)
                if current.casefold()==(prefix+'Payloads/kernel.macho.adt').casefold():assert b==updated
                elif current.casefold()==(prefix+'unixnu.toml').casefold():assert b==menu.encode()
                else:assert a==b;unchanged[current]=sha(b)
    audit_dir(fat.root_cluster,'')
    newraw=raw[:0x400080]+payload.read_bytes();compressed=gzip.compress(newraw,mtime=0)
    header=bytearray(image[:4096]);assert struct.unpack_from('<I',header,12)[0]==0
    struct.pack_into('<I',header,8,len(compressed));unsigned=bytes(header)+compressed
    unsigned+=bytes(-len(unsigned)%4096);(OUT/'boot.img').write_bytes(unsigned)
    assert gzip.decompress(unsigned[4096:4096+len(compressed)])==newraw
    r=dict(source_sha256=EXPECTED,unsigned_sha256=sha(unsigned),adt_sha256=sha(updated),session=session,
           selection={'IOProviderClass':'IOMedia','IOPropertyMatch':{'UUID':UUID}},
           authblob_preserved=True,fd_shim_unchanged=True,unchanged_files=unchanged,
           runtime_change='standard root-matching replaces boot-uuid IOResources wait',hardware_tested=False)
    (OUT/'build-audit.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(r,indent=2));print(menu)

if __name__=='__main__':main()
