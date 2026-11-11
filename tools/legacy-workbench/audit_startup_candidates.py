"""Read-only audit of the final signed startup candidates and embedded inputs."""
from pathlib import Path
import gzip,hashlib,json,struct,sys,tomllib
from avb_guard import check_image,metadata
from audit_boot_candidate import Fat,PORT
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]

def adt_properties(data):
    nodes={}
    def walk(o,parent):
        n,c=struct.unpack_from('<II',data,o);o+=8;p={}
        for _ in range(n):
            key=data[o:o+32].split(b'\0')[0].decode();size=struct.unpack_from('<I',data,o+32)[0]&0x7fffffff;o+=36
            assert key not in p and o+size<=len(data);p[key]=data[o:o+size];o+=(size+3)&~3
        path=parent+'/'+p['name'].split(b'\0')[0].decode();assert path not in nodes;nodes[path]=p
        for _ in range(c):o=walk(o,path)
        return o
    assert walk(0,'')==len(data);return nodes

def trust_check(b):
    count=struct.unpack_from('<I',b)[0];assert 0<count<=128
    for i in range(count):
        a=struct.unpack_from('<I',b,4+4*i)[0];z=struct.unpack_from('<I',b,8+4*i)[0] if i+1<count else len(b)
        assert a>=4+4*count and a%4==0 and a+24<=z<=len(b)
        version=struct.unpack_from('<I',b,a)[0];n=struct.unpack_from('<I',b,a+20)[0]
        assert version==2 and z-a==24+24*n
        previous=None
        for j in range(n):
            item=b[a+24+j*24:a+48+j*24];assert 1<=item[20]<=4
            assert previous is None or previous<item[:20];previous=item[:20]
    return count

def packed_check(b,expected):
    magic,version,page,logical,count=struct.unpack_from('<8sIIQQ',b);assert magic==b'DBRDISK1' and version==1 and page==4096 and logical==536870912 and count<=65536
    cursor=32+16*count;end=0;h=hashlib.sha256();zeros=bytes(1<<20)
    for i in range(count):
        at,size=struct.unpack_from('<QQ',b,32+16*i);assert size and (at|size)%4096==0 and at>=end and size<=logical-at and size<=len(b)-cursor
        gap=at-end
        while gap:n=min(gap,len(zeros));h.update(zeros[:n]);gap-=n
        h.update(b[cursor:cursor+size]);cursor+=size;end=at+size
    while end<logical:n=min(logical-end,len(zeros));h.update(zeros[:n]);end+=n
    assert cursor==len(b) and h.hexdigest()==expected
    return count

def main():
    root=ROOT/'edl_work/startup-candidates-20261107';reports=[];same=None
    baseline=(ROOT/'edl_work/ipados-console-entry-20261107/boot-signed.img').read_bytes()
    baseline=gzip.decompress(baseline[4096:4096+struct.unpack_from('<I',baseline,8)[0]])
    old_props=metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
    for mode in ('manual','autostart'):
        work=root/mode;p=json.loads((work/'packet.json').read_text());image=work/'boot-signed.img';a=check_image(image,'boot_a')
        assert metadata(image)['props']==old_props and a.bytes==100663296
        b=image.read_bytes();raw=gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
        assert raw[:0x400080]==baseline[:0x400080]
        fat=Fat(raw[0x400080:]);kernel=fat.read('/EFI/UniXNU/Payloads/kernel.macho');adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
        trust=fat.read('/EFI/UniXNU/Payloads/kernel.macho.trustcache');data=fat.read('/EFI/UniXNU/Payloads/data.packed')
        for blob,key in ((kernel,'kernel_sha256'),(adt,'adt_sha256'),(trust,'trust_sha256'),(data,'ram_data_packed_sha256')):assert hashlib.sha256(blob).hexdigest()==p[key]
        if same is None:
            same=(kernel,adt,trust,data);modules=trust_check(trust)
            extents=packed_check(data,'8104894952be1abadd7abaacbf2ca8d823c9789c525e2a3cbb2e57369745df33')
        else:assert same==(kernel,adt,trust,data)
        nodes=adt_properties(adt);chosen=nodes['/device-tree/chosen'];mem=nodes['/device-tree/chosen/memory-map']
        assert struct.unpack('<Q',chosen['darwinboot-memory-size'])[0]==0x180000000
        assert struct.unpack('<Q',chosen['dram-base'])[0]==0x8c0000000 and struct.unpack('<Q',chosen['dram-size'])[0]==0x340000000
        assert mem['RAMDisk']==mem['TrustCache']==bytes(16) and 'GpuAddon' not in mem
        assert 'boot-manifest-hash' not in chosen and 'owner-boot-manifest-domain' not in chosen
        assert len([x for x in chosen['text-fixups'].split(b'\0') if x])==4
        assert nodes['/device-tree/arm-io/intc']['AAPL,phandle']==struct.pack('<I',1)
        assert adt_walk(adt)['window']==0x180000000;parsed=parse_kernel(kernel)
        symbols=maker.symbols(kernel)
        required=('_elden_usb_log_putc','_elden_usb_panic_drain','_elden_note_user_trap','_dwc3_init','_dwc3_poll',
                  '_native_verified_block_root_authorized','_native_verified_block_volume_device','_native_verified_block_manifest_sha384',
                  '_native_owner_cache_status','_native_owner_data_status','_native_owner_boot_manifest_hash','_elden_init_exec_accepted')
        assert all(name in symbols for name in required)
        assert struct.unpack_from('<I',kernel,maker.locate(kernel,0xfffffff004aaecfc)[1])[0]==0x17ffffd4
        config=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode());entries=config['entry']
        assert config['boot']['timeout']==(120 if mode=='manual' else 8)
        assert config['boot']['default']==('shutdown' if mode=='manual' else 'xnu')
        assert len({e['id'] for e in entries})==len(entries)
        for e in entries:
            if e['type']=='xnu':
                assert e['path']=='Payloads/kernel.macho' and e['initrd']=='Payloads/data.packed'
                assert 'elden-debug-session='+p['session_id'] in e['args']
                assert len(e['args'])<1024
        first=next(e for e in entries if e['id']=='xnu')
        assert all(x in first['args'] for x in ('native-ufs-root=1','native-ufs-lun=0','elden-usb-debug=1','elden-usb-required=1','rd=uuid','boot-uuid=04769D26-2CA0-4825-B443-E2E5F88300F2'))
        r=dict(mode=mode,image_sha256=a.sha256,index=a.index,location=a.location,session_id=p['session_id'],
               fd_and_shim_unchanged=True,kernel_sha256=p['kernel_sha256'],defined_required_symbols=len(required),
               trust_modules=modules,ram_data_extents=extents,ram_data_reconstructed_sha256=True,
               memory_window=6442450944,read_only_ufs=True,device_access=False,hardware_tested=False)
        (work/'independent-audit.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');reports.append(r)
    (root/'audit.json').write_text(json.dumps(reports,indent=2)+'\n',encoding='utf-8');print(json.dumps(reports,indent=2))

if __name__=='__main__':main()
