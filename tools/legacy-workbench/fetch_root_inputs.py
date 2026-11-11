"""Fetch only the selected root images from the pinned official Apple IPSW.

Uses HTTP ranges and SHA256 values in the selected local profile. Existing
verified components are reused. No device access, unpacked software execution,
or full-IPSW download is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import time
import tomllib
import urllib.request
import zipfile
import zlib


class RemoteZip:
    def __init__(self,url):
        if not url.startswith('https://updates.cdn-apple.com/'):
            raise ValueError('expected pinned Apple CDN URL')
        self.url=url;self.pos=0;self.cache={};self.block=1<<20
        with self.request(0,0) as response:
            self.size=int(response.headers['Content-Range'].split('/')[-1]);response.read()
    def request(self,start,end):
        req=urllib.request.Request(self.url,headers={'Range':f'bytes={start}-{end}','User-Agent':'Y700-offline-input-preparation'})
        response=urllib.request.urlopen(req,timeout=90)
        content=response.headers.get('Content-Range','')
        if response.status!=206 or not content.startswith(f'bytes {start}-{end}/'):
            response.close();raise IOError('server did not honor exact range')
        return response
    def seek(self,offset,whence=0):
        pos=offset if whence==0 else self.pos+offset if whence==1 else self.size+offset
        if not 0<=pos<=self.size:raise ValueError('remote seek outside archive')
        self.pos=pos;return pos
    def tell(self):return self.pos
    def seekable(self):return True
    def read(self,size=-1):
        if size<0:size=self.size-self.pos
        size=min(size,self.size-self.pos)
        if size>32<<20:raise ValueError('oversized ZIP metadata read')
        result=bytearray()
        while size:
            index=self.pos//self.block
            if index not in self.cache:
                start=index*self.block;end=min(self.size,start+self.block)-1
                with self.request(start,end) as response:data=response.read()
                if len(data)!=end-start+1:raise IOError('short metadata range')
                if len(self.cache)>=3:self.cache.pop(next(iter(self.cache)))
                self.cache[index]=data
            data=self.cache[index];within=self.pos%self.block;take=min(size,len(data)-within)
            if not take:raise IOError('empty metadata block')
            result+=data[within:within+take];self.pos+=take;size-=take
        return bytes(result)


def digest(path):
    sha=hashlib.sha256();crc=0
    with path.open('rb') as stream:
        while chunk:=stream.read(8<<20):sha.update(chunk);crc=zlib.crc32(chunk,crc)
    return sha,crc


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();profile=tomllib.loads(a.profile.read_text(encoding='utf-8'))['ipsw']
    if profile['build']!='21H461' or profile['version']!='17.7.11':raise ValueError('unexpected OS profile')
    a.output.mkdir(parents=True,exist_ok=True)
    remote=RemoteZip(profile['url']);archive=zipfile.ZipFile(remote)
    manifest=archive.read('BuildManifest.plist');bm=plistlib.loads(manifest)
    if bm['ProductBuildVersion']!='21H461' or bm['ProductVersion']!='17.7.11':raise ValueError('BuildManifest version mismatch')
    identities=[i for i in bm['BuildIdentities'] if i.get('Info',{}).get('DeviceClass')==profile['device_class']]
    if not identities:raise ValueError('device class missing from BuildManifest')
    roles={}
    for item in identities:
        for name,value in item['Manifest'].items():
            path=value.get('Info',{}).get('Path')
            if path in profile['sha256']:roles.setdefault(path,[]).append(name)
    if set(roles)!=set(profile['sha256']):raise ValueError('pinned files not bound to selected BuildManifest identity')
    (a.output/'BuildManifest.plist').write_bytes(manifest)
    report=dict(url=profile['url'],profile_sha256=hashlib.sha256(a.profile.read_bytes()).hexdigest(),
        build='21H461',version='17.7.11',device_class=profile['device_class'],
        BuildManifest_sha256=hashlib.sha256(manifest).hexdigest(),archive_bytes=remote.size,files=[],complete=False)
    for name,wanted in profile['sha256'].items():
        info=archive.getinfo(name);target=a.output/name;partial=a.output/(name+'.partial')
        if target.exists():
            sha,crc=digest(target)
            if target.stat().st_size!=info.file_size or sha.hexdigest()!=wanted:raise ValueError('existing named input differs: '+name)
            report['files'].append(dict(path=name,roles=sorted(set(roles[name])),bytes=info.file_size,sha256=wanted,reused=True));continue
        if info.compress_type not in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
            raise ValueError('unsupported selected-member compression')
        if info.flag_bits&1:raise ValueError('encrypted ZIP member unsupported')
        remote.seek(info.header_offset);header=remote.read(30)
        if header[:4]!=b'PK\x03\x04':raise ValueError('invalid local ZIP header')
        name_len,extra_len=struct.unpack_from('<HH',header,26)
        local_name=remote.read(name_len).decode('utf-8')
        if local_name!=name:raise ValueError('central/local member name mismatch')
        offset=info.header_offset+30+name_len+extra_len
        if info.compress_type==zipfile.ZIP_DEFLATED:
            compressed=a.output/(name+'.zipdeflate.partial')
            count=compressed.stat().st_size if compressed.exists() else 0
            if count>info.compress_size:raise ValueError('oversized compressed partial')
            if shutil.disk_usage(a.output).free<info.compress_size-count+info.file_size+(256<<20):raise ValueError('insufficient host space')
            print(f'{name}: selected deflate member {info.compress_size} -> {info.file_size} bytes, resume={count}',flush=True)
            if count<info.compress_size:
                last=time.monotonic()
                with remote.request(offset+count,offset+info.compress_size-1) as response,compressed.open('ab') as stream:
                    while count<info.compress_size:
                        chunk=response.read(min(8<<20,info.compress_size-count))
                        if not chunk:raise IOError('short compressed-member transfer')
                        stream.write(chunk);count+=len(chunk)
                        if time.monotonic()-last>=10:print(f'{name}: compressed {count}/{info.compress_size}',flush=True);last=time.monotonic()
                    stream.flush();os.fsync(stream.fileno())
            decoder=zlib.decompressobj(-15);sha=hashlib.sha256();crc=0;done=0
            with compressed.open('rb') as source,partial.open('wb') as output:
                while chunk:=source.read(4<<20):
                    pending=chunk
                    while pending:
                        raw=decoder.decompress(pending,8<<20);pending=decoder.unconsumed_tail
                        done+=len(raw)
                        if done>info.file_size:raise ValueError('deflate member exceeds declared size')
                        output.write(raw);sha.update(raw);crc=zlib.crc32(raw,crc)
                raw=decoder.flush();done+=len(raw)
                if done!=info.file_size or not decoder.eof or decoder.unused_data:raise ValueError('incomplete or excessive deflate data')
                output.write(raw);sha.update(raw);crc=zlib.crc32(raw,crc);output.flush();os.fsync(output.fileno())
            if sha.hexdigest()!=wanted or (crc&0xffffffff)!=info.CRC:raise ValueError('deflated input SHA/CRC mismatch')
            partial.rename(target);compressed.unlink()
            report['files'].append(dict(path=name,roles=sorted(set(roles[name])),bytes=info.file_size,sha256=wanted,reused=False))
            (a.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(name+': SHA256/ZIP CRC verified',flush=True)
            continue
        done=partial.stat().st_size if partial.exists() else 0
        if done>info.file_size:raise ValueError('oversized partial input')
        if shutil.disk_usage(a.output).free<info.file_size-done+(256<<20):raise ValueError('insufficient host space')
        sha,crc=digest(partial) if done else (hashlib.sha256(),0)
        print(f'{name}: role={sorted(set(roles[name]))}, {info.file_size} bytes, resume={done}',flush=True)
        last=time.monotonic()
        if done<info.file_size:
            with remote.request(offset+done,offset+info.file_size-1) as response,partial.open('ab') as output:
                while done<info.file_size:
                    chunk=response.read(min(8<<20,info.file_size-done))
                    if not chunk:raise IOError('short file transfer; partial retained')
                    output.write(chunk);sha.update(chunk);crc=zlib.crc32(chunk,crc);done+=len(chunk)
                    if time.monotonic()-last>=10:
                        print(f'{name}: {done}/{info.file_size}',flush=True);last=time.monotonic()
                output.flush();os.fsync(output.fileno())
        if sha.hexdigest()!=wanted or (crc&0xffffffff)!=info.CRC:raise ValueError('download hash/CRC mismatch; partial retained')
        partial.rename(target)
        report['files'].append(dict(path=name,roles=sorted(set(roles[name])),bytes=info.file_size,sha256=wanted,reused=False))
        (a.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(name+': SHA256/ZIP CRC verified',flush=True)
    report['complete']=True
    (a.output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
