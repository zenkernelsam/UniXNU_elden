"""Map read-only saved user stack samples into the exact original 21H461 cache."""
from pathlib import Path
import bisect,json,mmap,plistlib,struct,subprocess
root=Path.cwd();out=root/'research/icon-fault-trace-20261111/live-v89'
fields=list(map(int,subprocess.check_output([root/'research/userfault-symbols-20261107/offsets'],text=True).split()))
textoff,localoff,regionoff,textsize,infosize,entrysize,uuidfield,slidefield,slidesize=fields
assert (textsize,infosize,entrysize)==(32,24,16)
point=out/'mount';point.mkdir(exist_ok=True);assert not any(point.iterdir())
attached=plistlib.loads(subprocess.check_output(['hdiutil','attach','-readonly','-nobrowse','-owners','off','-imagekey','diskimage-class=CRawDiskImage','-mountpoint',point,'-plist',root/'out/prerequisites-20261106/root-images/SystemOS.raw']))
dev=next(e['dev-entry'] for e in attached['system-entities'] if 'dev-entry' in e)
try:
    cache=point/'System/Library/Caches/com.apple.dyld';main=(cache/'dyld_shared_cache_arm64').read_bytes()
    region=struct.unpack_from('<Q',main,regionoff)[0];table,count=struct.unpack_from('<QQ',main,textoff)
    images=[]
    for i in range(count):
        _,va,size,path=struct.unpack_from('<16sQII',main,table+i*textsize)
        end=main.find(b'\0',path,path+2048);assert end>=path;images.append((va,size,main[path:end].decode()))
    images.sort();starts=[i[0] for i in images]
    samples=json.loads((out/'samples.json').read_text());symbol_cache={}
    with (cache/'dyld_shared_cache_arm64.symbols').open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as symbols:
        assert symbols[88:104]==main[uuidfield:uuidfield+16]
        local,_=struct.unpack_from('<QQ',symbols,localoff);no,nc,so,ss,eo,ec=struct.unpack_from('<6I',symbols,local)
        entries={struct.unpack_from('<Q',symbols,local+eo+i*entrysize)[0]:struct.unpack_from('<II',symbols,local+eo+i*entrysize+8) for i in range(ec)}
        def resolve(address,slide):
            va=address-slide;index=bisect.bisect_right(starts,va)-1
            if index<0:return dict(address=hex(address),outside_cache=True)
            im=images[index]
            if not im[0]<=va<im[0]+im[1]:return dict(address=hex(address),outside_cache=True)
            row=dict(address=hex(address),canonical=hex(va),image=im[2])
            if im[0] not in symbol_cache:
                items=[]
                if im[0]-region in entries:
                    start,n=entries[im[0]-region];assert start+n<=nc
                    for i in range(start,start+n):
                        string,kind,section,_,value=struct.unpack_from('<IBBHQ',symbols,local+no+i*16)
                        if not kind&0xe0 and kind&0xe==0xe and section and im[0]<=value<im[0]+im[1]:
                            assert string<ss;pos=local+so+string;end=symbols.find(b'\0',pos)
                            items.append((value,symbols[pos:end].decode(errors='replace')))
                symbol_cache[im[0]]=sorted(items)
            items=symbol_cache[im[0]];j=bisect.bisect_right(items,(va,chr(0x10ffff)))-1
            if j>=0:row.update(symbol=items[j][1],offset=hex(va-items[j][0]))
            return row
        for sample in samples:
            sample['resolved']=[resolve(a,sample['slide']) for a in [sample['pc'],sample['lr'],*sample['frames']] if a]
    catalog=[]
    for p in cache.glob('dyld_shared_cache_arm64*'):
        if p.suffix=='.symbols':continue
        with p.open('rb') as f:
            header=f.read(32)
            if not header.startswith(b'dyld_v1   arm64'):continue
            mo,mc=struct.unpack_from('<II',header,16);assert 0<mc<100
            f.seek(mo)
            for i in range(mc):
                va,size,offset,maximum,prot=struct.unpack('<QQQII',f.read(32));catalog.append((p,va,size,offset))
    def read(va,n):
        for p,start,size,off in catalog:
            if start<=va and va+n<=start+size:
                with p.open('rb') as f:f.seek(off+va-start);data=f.read(n)
                assert len(data)==n;return data
        raise ValueError(hex(va))
    va=0x19ce2aac4-0x5730000
    (out/'fault-window.json').write_text(json.dumps(dict(start=va-0x180,return_address=va,bytes=read(va-0x180,0x240).hex()),indent=2))
    image=next(i for i in images if i[2].endswith('/CoreImage'))
    h=read(image[0],32);n,sz=struct.unpack_from('<II',h,16);lc=read(image[0]+32,sz);at=0;strings=[]
    for i in range(n):
        cmd,size=struct.unpack_from('<II',lc,at)
        if cmd==0x19:
            ns=struct.unpack_from('<I',lc,at+64)[0]
            for j in range(ns):
                section=lc[at+72+80*j:at+152+80*j];name=section[:16].split(b'\0')[0]
                if name in (b'__cstring',b'__objc_methname'):
                    addr,length=struct.unpack_from('<QQ',section,32);assert length<32*1024*1024
                    data=read(addr,length);pos=0
                    for part in data.split(b'\0'):
                        if any(q in part.lower() for q in (b'software',b'force',b'ci_',b'glcontext')):strings.append(dict(address=hex(addr+pos),text=part.decode(errors='replace')))
                        pos+=len(part)+1
        at+=size
    (out/'coreimage-options.json').write_text(json.dumps(strings,indent=2))
    (out/'symbolication.json').write_text(json.dumps(dict(original_symbols_uuid_matched=True,atomic_thread_snapshot=False,samples=samples),indent=2)+'\n')
    for sample in samples:
        if sample['name']=='iconservicesagent':
            print(sample['fault_index'],sample['name'],sample['pid'],sample['reason'])
            for row in sample['resolved'][:18]:print(' ',row.get('symbol',row.get('canonical',row['address'])),row.get('offset',''))
finally:subprocess.run(['hdiutil','detach',dev],check=True)
