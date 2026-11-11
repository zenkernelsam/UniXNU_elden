"""Decode only the Apple_APFS region from the verified OS UDIF image.

Each run is bounded and decoded independently; the Apple-provided blkx CRC32
checks the complete raw region. No image is mounted and no device is opened.
"""
import argparse
import bz2
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import sys
import time
import zlib


def verify_raw(path,runs,total,expected_crc):
    if path.stat().st_size!=total:raise ValueError('raw file size mismatch')
    sha=hashlib.sha256();crc=0
    with path.open('rb') as stream:
        for kind,start,n,_,_ in runs:
            stream.seek(start*512);remaining=n*512
            while remaining:
                block=stream.read(min(8<<20,remaining))
                if not block:raise IOError('raw verification short read')
                if kind in (0,2) and block.count(0)!=len(block):raise ValueError('zero/ignored extent contains data')
                sha.update(block)
                # UDIF IGNORE extents have no checksum contribution. They are
                # materialized as zero holes; ZERO extents do contribute.
                if kind!=2:crc=zlib.crc32(block,crc)
                remaining-=len(block)
        stream.seek(0);header=stream.read(4096)
    if header[32:36]!=b'NXSB' or crc&0xffffffff!=expected_crc:raise ValueError('raw APFS/Apple blkx CRC verification failed')
    return sha.hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--expected-sha256',required=True)
    p.add_argument('--verify-partial',action='store_true',help='validate a complete prior partial without decoding/downloading again')
    p.add_argument('--pydeps',type=Path,default=Path(__file__).resolve().parent/'prerequisites-20261106/pydeps')
    a=p.parse_args()
    sys.path.insert(0,str(a.pydeps.resolve()));import lzfse
    if a.output.exists():raise ValueError('output already exists; verify/reuse it instead')
    sha=hashlib.sha256()
    with a.input.open('rb') as stream:
        while block:=stream.read(8<<20):sha.update(block)
        size=stream.tell();stream.seek(-512,2);footer=stream.read(512)
        if sha.hexdigest()!=a.expected_sha256:raise ValueError('source SHA256 mismatch')
        if footer[:4]!=b'koly' or struct.unpack_from('>I',footer,8)[0]!=512:raise ValueError('invalid UDIF footer')
        fork,fork_bytes=struct.unpack_from('>QQ',footer,24);xml_at,xml_bytes=struct.unpack_from('>QQ',footer,216)
        if not xml_bytes or xml_bytes>16<<20 or xml_at+xml_bytes>size-512:raise ValueError('UDIF metadata bounds')
        stream.seek(xml_at);doc=plistlib.loads(stream.read(xml_bytes))
    matches=[x for x in doc['resource-fork']['blkx'] if x.get('Name')=='Apple_APFS']
    if len(matches)!=1:raise ValueError('expected one APFS region')
    table=matches[0]['Data']
    if table[:4]!=b'mish' or len(table)<204:raise ValueError('invalid blkx table')
    first,sectors,base=struct.unpack_from('>QQQ',table,8);count=struct.unpack_from('>I',table,200)[0]
    if not 1<=count<=100000 or len(table)!=204+count*40:raise ValueError('blkx count/length mismatch')
    check_type,check_bits,expected_crc=struct.unpack_from('>III',table,64)
    if (check_type,check_bits)!=(2,32):raise ValueError('independent CRC32 checksum unavailable')
    total=sectors*512;runs=[];next_sector=0
    for i in range(count):
        kind,_,start,n,offset,compressed=struct.unpack_from('>IIQQQQ',table,204+40*i)
        if kind==0xffffffff:
            if i!=count-1 or start!=sectors or n or compressed:raise ValueError('bad blkx terminator')
            continue
        if kind not in (0,1,2,0x80000005,0x80000006,0x80000007) or start!=next_sector or not n or start+n>sectors:
            raise ValueError('unsupported/noncontiguous blkx run')
        if kind in (0,2):
            if compressed:raise ValueError('zero run has source bytes')
        elif fork+base+offset+compressed>fork+fork_bytes or fork+base+offset<fork or not compressed:
            raise ValueError('compressed run outside data fork')
        if kind not in (0,1,2) and (n*512>8<<20 or compressed>16<<20):raise ValueError('oversized compressed run')
        if kind==1 and compressed!=n*512:raise ValueError('raw run size mismatch')
        runs.append((kind,start,n,offset,compressed));next_sector=start+n
    if next_sector!=sectors or not total:raise ValueError('incomplete APFS region')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(a.output.parent).free<total+(256<<20):raise ValueError('insufficient host free space')
    temporary=a.output.with_name(a.output.name+'.partial')
    if a.verify_partial:
        final_sha=verify_raw(temporary,runs,total,expected_crc)
        temporary.rename(a.output)
        report=dict(source=str(a.input),source_sha256=a.expected_sha256,output=str(a.output),bytes=total,
                    sha256=final_sha,source_first_sector=first,blkx_runs=len(runs),apple_blkx_crc32=f'{expected_crc:08x}',
                    checksum_scope='all decoded extents except UDIF IGNORE; ignored extents verified zero',
                    full_readback=True,reused_completed_partial=True,device_access=False)
        a.output.with_suffix(a.output.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,indent=2));return
    if temporary.exists():raise ValueError('partial output exists; inspect before replacing')
    digest=hashlib.sha256();crc=0;zeros=bytes(8<<20);last=time.monotonic()
    with a.input.open('rb') as source,temporary.open('xb') as output:
        for index,(kind,start,n,offset,compressed) in enumerate(runs):
            raw_bytes=n*512
            if kind in (0,2):
                output.seek(raw_bytes,1);remaining=raw_bytes
                while remaining:
                    chunk=zeros[:min(remaining,len(zeros))];digest.update(chunk)
                    if kind==0:crc=zlib.crc32(chunk,crc)
                    remaining-=len(chunk)
            elif kind==1:
                source.seek(fork+base+offset);remaining=raw_bytes
                while remaining:
                    chunk=source.read(min(8<<20,remaining))
                    if not chunk:raise IOError('short raw run')
                    output.write(chunk);digest.update(chunk);crc=zlib.crc32(chunk,crc);remaining-=len(chunk)
            else:
                source.seek(fork+base+offset);chunk=source.read(compressed)
                if len(chunk)!=compressed:raise IOError('short compressed run')
                raw=lzfse.decompress(chunk) if kind==0x80000007 else zlib.decompress(chunk) if kind==0x80000005 else bz2.decompress(chunk)
                if len(raw)!=raw_bytes:raise ValueError('decoded run size mismatch')
                output.write(raw);digest.update(raw);crc=zlib.crc32(raw,crc)
            if time.monotonic()-last>=10:print(f'APFS decode: {index+1}/{len(runs)} runs',flush=True);last=time.monotonic()
        output.truncate(total);output.flush();os.fsync(output.fileno())
    if crc&0xffffffff!=expected_crc:raise ValueError('Apple blkx CRC32 mismatch; partial retained')
    # Independent complete file read-back, including sparse/zero runs.
    if verify_raw(temporary,runs,total,expected_crc)!=digest.hexdigest():raise ValueError('raw APFS signature/readback mismatch')
    temporary.rename(a.output)
    report=dict(source=str(a.input),source_sha256=a.expected_sha256,output=str(a.output),
        bytes=total,sha256=digest.hexdigest(),source_first_sector=first,blkx_runs=len(runs),
        apple_blkx_crc32=f'{expected_crc:08x}',checksum_scope='all decoded extents except UDIF IGNORE; ignored extents verified zero',
        full_readback=True,device_access=False)
    a.output.with_suffix(a.output.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
