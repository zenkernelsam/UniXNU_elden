"""Plan/validate SHNGROOT v1 windows for the current read-only NativeUfs ABI.

Reads local regular files only. Emits a 4KiB header and a manifest. Optional
--build-image constructs a new local image; it never opens a block device.
Filesystem mountability, volume identity and device readiness remain separate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import uuid

PROFILE='ipados-17.7.11-21H461'
ALIGN=1<<20


def aligned(value,unit=4096):return (value+unit-1)//unit*unit


def file_hash(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(8<<20):h.update(chunk)
    return h.hexdigest()


def inspect_raw(path):
    info=path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_size<4096 or info.st_size%512:
        raise ValueError('a sector-aligned local regular image is required: '+str(path))
    with path.open('rb') as stream:
        first=stream.read(4096);stream.seek(-512,os.SEEK_END);tail=stream.read(512)
    if tail[:4]==b'koly':raise ValueError('UDIF/DMG container must be converted to raw blocks first: '+str(path))
    if first[32:36]==b'NXSB':
        block,blocks=struct.unpack_from('<IQ',first,36)
        if block not in (4096,8192,16384,32768,65536) or not blocks or blocks*block>info.st_size:
            raise ValueError('APFS container geometry exceeds input')
        with path.open('rb') as stream:superblock=stream.read(block)
        modulus=0xffffffff;s1=s2=0
        for word in struct.unpack('<'+str((block-8)//4)+'I',superblock[8:]):
            s1=(s1+word)%modulus;s2=(s2+s1)%modulus
        c1=modulus-((s1+s2)%modulus);c2=modulus-((s1+c1)%modulus)
        if struct.unpack_from('<Q',superblock)[0]!=(c2<<32)|c1:raise ValueError('APFS container superblock checksum mismatch')
        result=dict(format='APFS container',filesystem_bytes=blocks*block,
                    container_uuid=str(uuid.UUID(bytes=first[72:88])))
    elif first[1024:1026] in (b'H+',b'HX'):
        block,blocks=struct.unpack_from('>II',first,1064)
        if block<512 or block&(block-1) or not blocks or blocks*block>info.st_size:
            raise ValueError('HFS+ geometry exceeds input')
        result=dict(format='HFS+/HFSX volume',filesystem_bytes=blocks*block,container_uuid=None)
    else:raise ValueError('not a recognized raw APFS/HFS+ input: '+str(path))
    return dict(bytes=info.st_size,sha256=file_hash(path),**result)


def decode_header(header,partition_bytes):
    if len(header)!=4096 or header[:8]!=b'SHNGROOT' or hashlib.sha256(header[:4064]).digest()!=header[4064:]:
        raise ValueError('manifest magic/length/SHA mismatch')
    version,count,total=struct.unpack_from('<IIQ',header,8)
    if version!=1 or not 1<=count<=4 or total<4096 or total%4096 or total>partition_bytes:
        raise ValueError('manifest version/count/image bounds')
    windows=[]
    for i in range(count):
        row=header[24+128*i:24+128*(i+1)];end=row[:32].find(b'\0')
        if not 0<end<32 or any(c<0x20 or c>0x7e for c in row[:end]):raise ValueError('bad window name')
        name=row[:end].decode();offset,size,sector,flags=struct.unpack_from('<QQII',row,32)
        if not size or size%512 or offset<4096 or offset%4096 or offset+aligned(size)>total or sector!=512 or flags!=1:
            raise ValueError('window bounds/attributes')
        for other in windows:
            if name==other['name']:raise ValueError('duplicate window name')
            if offset<other['offset']+aligned(other['bytes']) and other['offset']<offset+aligned(size):raise ValueError('overlapping physical windows')
        windows.append(dict(name=name,offset=offset,bytes=size,sha256=row[56:88].hex()))
    return dict(image_bytes=total,windows=windows)


def prepare(spec,base):
    if spec.get('profile')!=PROFILE:raise ValueError('wrong/missing OS profile')
    target=spec.get('target',{});capacity=target.get('bytes',0)
    if target.get('lun') not in (0,4) or not isinstance(capacity,int) or capacity<4096 or capacity%4096:
        raise ValueError('explicit LUN and aligned partition capacity required')
    if not target.get('partition') or target['partition'].lower() in ('userdata','metadata','super','boot_a','boot_b'):
        raise ValueError('a dedicated root partition is required; Android partitions are not root targets')
    entries=spec.get('windows',[])
    if not 2<=len(entries)<=4 or {e.get('name') for e in entries}!={'OS','SystemOS'}:
        raise ValueError('this preparation profile requires exactly OS and SystemOS windows')
    if len(entries)!=2:raise ValueError('duplicate or unsupported windows')
    header=bytearray(4096);header[:8]=b'SHNGROOT';rows=[];at=ALIGN
    for i,item in enumerate(entries):
        path=Path(item['path']);path=(base/path).resolve() if not path.is_absolute() else path.resolve()
        info=inspect_raw(path)
        if item.get('expected_sha256') and item['expected_sha256'].lower()!=info['sha256']:raise ValueError('input digest mismatch: '+item['name'])
        name=item['name'].encode('ascii');row=24+128*i;header[row:row+len(name)]=name
        struct.pack_into('<QQII',header,row+32,at,info['bytes'],512,1)
        header[row+56:row+88]=bytes.fromhex(info['sha256'])
        rows.append(dict(name=item['name'],path=str(path),offset=at,**info));at=aligned(at+info['bytes'],ALIGN)
    if at>capacity:raise ValueError('raw root images exceed the planned partition')
    struct.pack_into('<IIQ',header,8,1,len(rows),at);header[4064:]=hashlib.sha256(header[:4064]).digest()
    decoded=decode_header(header,capacity);assert decoded['image_bytes']==at
    volume=spec.get('root_volume_uuid')
    if volume:
        if uuid.UUID(volume).int==0:raise ValueError('zero root filesystem UUID')
        if any(volume.lower()==str(row.get('container_uuid','')).lower() for row in rows):
            raise ValueError('root volume UUID equals a container UUID; obtain the actual filesystem volume UUID')
    pending=['fresh GPT and device partition admission','real NativeUfs block publication and root mount',
             'kernel root target must change from diagnostic LUN4/ARMADA_BACKUP to this explicit target']
    if not volume:pending.append('actual root filesystem UUID; GPT/container UUID is insufficient')
    if not spec.get('extraction_provenance'):pending.append('bind raw images to the owned IPSW BuildManifest and extraction hashes')
    if spec.get('data',{}).get('mode')!='ram':pending.append('qualified Data strategy and memory budget')
    else:pending.append('verify RAM Data seed identity, contents and actual memory consumption')
    report=dict(profile=PROFILE,target=target,image_bytes=at,windows=rows,root_volume_uuid=volume,
        header_sha256=hashlib.sha256(header).hexdigest(),format='SHNGROOT v1',
        validation_scope='raw signatures, geometry, file hashes and driver-compatible header; not filesystem mount/integrity proof',
        ready_to_flash=False,device_access=False,pending=pending)
    return bytes(header),report


def build_image(header,report,destination):
    if destination.exists():raise ValueError('refusing to replace existing image')
    required=sum(row['bytes'] for row in report['windows'])+(64<<20)
    if shutil.disk_usage(destination.parent).free<required:raise ValueError('insufficient host free space')
    temporary=destination.with_name(destination.name+'.'+uuid.uuid4().hex+'.partial')
    with temporary.open('xb') as output:
        output.write(header)
        for row in report['windows']:
            output.seek(row['offset']);digest=hashlib.sha256()
            with Path(row['path']).open('rb') as stream:
                remaining=row['bytes']
                while remaining:
                    block=stream.read(min(8<<20,remaining))
                    if not block:raise IOError('input truncated during build')
                    digest.update(block);output.write(block);remaining-=len(block)
                if stream.read(1) or digest.hexdigest()!=row['sha256']:raise IOError('input changed during build')
        output.truncate(report['image_bytes']);output.flush();os.fsync(output.fileno())
    with temporary.open('rb') as stream:
        assert stream.read(4096)==header
        for row in report['windows']:
            stream.seek(row['offset']);digest=hashlib.sha256();remaining=row['bytes']
            while remaining:
                block=stream.read(min(8<<20,remaining))
                if not block:raise IOError('output short read')
                digest.update(block);remaining-=len(block)
            if digest.hexdigest()!=row['sha256']:raise IOError('output read-back mismatch')
    if destination.exists():raise ValueError('destination appeared while building')
    temporary.rename(destination)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spec',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--build-image',action='store_true')
    a=p.parse_args();header,report=prepare(json.loads(a.spec.read_text(encoding='utf-8')),a.spec.resolve().parent)
    if any((a.output/name).exists() for name in ('root-header.bin','root-plan.json','root.img')):
        raise ValueError('output artifacts already exist; choose a new output directory')
    a.output.mkdir(parents=True,exist_ok=True)
    if a.build_image:build_image(header,report,a.output/'root.img')
    (a.output/'root-header.bin').write_bytes(header)
    report['local_image_built']=a.build_image
    (a.output/'root-plan.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
