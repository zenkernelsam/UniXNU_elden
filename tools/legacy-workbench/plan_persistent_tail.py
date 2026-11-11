"""Offline-only layout proposal from complete post-partition GPT and SHNGROOT.

Writes JSON and a local header digest contract, never a disk image or a device.
The two stage slots, metadata and log remain separate from the fixed Data pool.
"""
from pathlib import Path
import argparse,hashlib,json,struct,uuid,zlib
ROOT=Path(__file__).resolve().parents[1]
BLOCK=4096
def sha(b):return hashlib.sha256(b).hexdigest()
def header(b,offset):
    h=bytearray(b[offset:offset+BLOCK]);assert h[:8]==b'EFI PART'
    size=struct.unpack_from('<I',h,12)[0];assert 92<=size<=BLOCK
    crc=struct.unpack_from('<I',h,16)[0];h[16:20]=bytes(4);assert zlib.crc32(h[:size])==crc
    current,backup,first,last=struct.unpack_from('<4Q',h,24)
    entries,count,entrybytes,arraycrc=struct.unpack_from('<QIII',h,72)
    assert struct.unpack_from('<I',h,8)[0]==0x10000 and entrybytes==128 and 0<count<=128
    return dict(current=current,backup=backup,first=first,last=last,disk=bytes(h[56:72]),
                entries=entries,count=count,arraycrc=arraycrc)
def array(b,h,base):
    offset=(h['entries']-base)*BLOCK;size=h['count']*128
    assert offset>=0 and offset+size<=len(b)
    data=b[offset:offset+size];assert zlib.crc32(data)==h['arraycrc'];return data
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'edl_work/persistent-data-20261108/tail-plan.json')
    a=p.parse_args();old=ROOT/'edl_work/partition-128g-20261106'
    pb=(old/'device-post-main.bin').read_bytes();bb=(old/'device-post-backup.bin').read_bytes()
    ph=header(pb,BLOCK);bh=header(bb,len(bb)-BLOCK)
    assert ph['current']==1 and bh['current']==ph['backup'] and bh['backup']==1
    assert all(ph[k]==bh[k] for k in ('disk','first','last','count','arraycrc'))
    pa=array(pb,ph,0);ba=array(bb,bh,bh['current']-(len(bb)//BLOCK-1));assert pa==ba
    found=[]
    for i in range(ph['count']):
        entry=pa[i*128:(i+1)*128]
        if entry[:16]==bytes(16):continue
        name=entry[56:128].decode('utf-16le').split('\0',1)[0]
        first,last,attrs=struct.unpack_from('<QQQ',entry,32)
        assert ph['first']<=first<=last<=ph['last']
        if name=='ipados':found.append((entry,first,last,attrs))
    assert len(found)==1
    entry,first,last,attrs=found[0]
    assert sha(entry)=='acd91e5236f9662bd2202c2afbe05556df9bbfde92b97a8bb89d6c6ee5996f21'
    assert str(uuid.UUID(bytes_le=entry[16:32]))=='2c04d4b8-e95c-466a-9436-308318ea82c8'
    assert str(uuid.UUID(bytes_le=entry[:16]))=='7c3457ef-0000-11aa-aa11-00306543ecac'
    assert first==215283456 and last==248838138 and attrs==0
    root=ROOT/'edl_work/prerequisites-20261106/root-prepared/root.img'
    with root.open('rb') as f:rh=f.read(BLOCK)
    assert rh[:8]==b'SHNGROOT' and sha(rh[:4064])==rh[4064:].hex()
    version,count,image=struct.unpack_from('<IIQ',rh,8)
    assert version==1 and count==2 and image==9381609472 and root.stat().st_size==image
    windows=[]
    for i in range(count):
        e=rh[24+i*128:24+(i+1)*128];offset,size=struct.unpack_from('<QQ',e,32)
        assert offset%BLOCK==0 and size and offset+size<=image
        windows.append({'name':e[:32].split(b'\0',1)[0].decode(),'offset':offset,'bytes':size,'sha256':e[56:88].hex()})
    ranges=[]
    def add(name,first,blocks):
        ranges.append({'name':name,'first_lba':first,'blocks':blocks,'last_lba':first+blocks-1,
                       'offset_bytes':(first-215283456)*BLOCK,'bytes':blocks*BLOCK})
        return first+blocks
    cursor=add('root-image',first,image//BLOCK)
    end=(cursor+1024+511)&~511;cursor=add('root-guard',cursor,end-cursor)
    cursor=add('layout-metadata',cursor,512)
    cursor=add('stage-current',cursor,2*1024**3//BLOCK);cursor=add('stage-previous',cursor,2*1024**3//BLOCK)
    pages=112*1024**3//BLOCK;maxwindow=1+(pages+32767)//32768+pages
    cursor=add('data-pool',cursor,maxwindow)
    logfirst=last+1-256*1024**2//BLOCK;assert cursor<logfirst
    cursor=add('unassigned',cursor,logfirst-cursor);cursor=add('log-reserve',cursor,256*1024**2//BLOCK)
    assert cursor==last+1
    for i,r in enumerate(ranges):
        assert r['blocks'] and first<=r['first_lba']<=r['last_lba']<=last
        for q in ranges[:i]:assert r['first_lba']>q['last_lba'] or q['first_lba']>r['last_lba']
    plan={'version':1,'proposal_only':True,'device_read_this_turn':False,'device_written':False,
          'historical_gpt_primary_sha256':sha(pb),'historical_gpt_backup_sha256':sha(bb),
          'entry_sha256':sha(entry),'partition_first':first,'partition_last':last,
          'root_header_sha256':sha(rh),'root_image_bytes':image,'root_windows':windows,'block_bytes':BLOCK,
          'logical_data_max_bytes':112*1024**3,'initial_logical_data_bytes':512*1024**2,
          'initial_overlay_window_blocks':131077,'fixed_pool_does_not_move_with_seed_size':True,
          'ranges':ranges,'write_gate':'Fresh device GPT/root/header and backup; M3 power qualification; explicit reviewed mode; bounded native owner; no old header replacement.',
          'security':'Existing RAM KeyStore is not permitted for persistent Data. Secure key storage/encryption remain a separate required gate.'}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'output':str(a.output),'root_header_sha256':plan['root_header_sha256'],
                      'ranges':[{k:r[k] for k in ('name','first_lba','blocks','bytes')} for r in ranges],
                      'device_written':False},indent=2))
if __name__=='__main__':main()
