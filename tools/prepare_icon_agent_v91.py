#!/usr/bin/env python3
"""Regenerate v91's private payload header from owner-supplied 21H461 inputs.

Preserves original executable sections and entitlements; only the local copy's
dependency command, UUID and ad-hoc signature change. No deployment is performed.
"""
from pathlib import Path
import argparse,hashlib,json,plistlib,struct,subprocess,sys

ORIGINAL='dd01b74e4413e0bb72de4a62f3f7675deb5026cad79d376fde6570abd0d012f8'
LIBRARY='f748e9cf601db43167eaf7736b0a161771b8c01eeb46860820604ccf892ac034'
HEADER='8fe7e098bf56d6d71ee02f885f1e5166c1ae95ad77497d0a08b30adf8835a143'
sha=lambda b:hashlib.sha256(b).hexdigest()

def prepare(b):
    if sha(b)!=ORIGINAL:raise ValueError('Expected original 21H461 iconservicesagent')
    n,sz=struct.unpack_from('<II',b,16);at=32;sections={};identifier=None;uuid_offset=None
    for _ in range(n):
        cmd,size=struct.unpack_from('<II',b,at)
        if size<8 or at+size>32+sz:raise ValueError('Invalid load command')
        if cmd==0x19:
            seg=b[at+8:at+24].split(b'\0')[0].decode();count=struct.unpack_from('<I',b,at+64)[0]
            for i in range(count):
                s=at+72+i*80;name=b[s:s+16].split(b'\0')[0].decode();_,length,off=struct.unpack_from('<QQI',b,s+32)
                if seg=='__TEXT':sections[name]={'offset':off,'bytes':length,'sha256':sha(b[off:off+length])}
        if cmd==0x1d:
            off,length=struct.unpack_from('<II',b,at+8);blob=b[off:off+length];_,_,count=struct.unpack_from('>III',blob)
            for i in range(count):
                slot,pos=struct.unpack_from('>II',blob,12+i*8)
                if slot==0:
                    start=pos+struct.unpack_from('>I',blob,pos+20)[0];identifier=blob[start:blob.index(b'\0',start)].decode()
        if cmd==0x1b:uuid_offset=at+8
        at+=size
    dependency='/System/Library/CoreServices/EldenIconCpuMode.dylib'
    size=(24+len(dependency)+1+7)&~7;start=32+sz
    first=min(x['offset'] for x in sections.values() if x['offset'])
    if not identifier or uuid_offset is None or start+size>first or any(b[start:first]):raise ValueError('Unexpected executable layout')
    command=struct.pack('<6I',0xc,size,24,0,0,0)+dependency.encode()+b'\0';command+=bytes(size-len(command))
    new=bytearray(b);struct.pack_into('<II',new,16,n+1,sz+size);new[start:start+size]=command
    uuid=hashlib.sha256(b+dependency.encode()+b'elden-normal-dependency').digest()[:16]
    new[uuid_offset:uuid_offset+16]=uuid
    return bytes(new),identifier,sections

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original-agent',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if sys.platform!='darwin':raise SystemExit('macOS codesign is required')
    original=a.original_agent.resolve(strict=True);b=original.read_bytes();lib=a.library.read_bytes()
    if sha(lib)!=LIBRARY:raise SystemExit('Rebuild the exact frozen v91 library first')
    unsigned,identifier,sections=prepare(b);out=a.output.resolve()
    if out.exists():raise SystemExit('Output must be new')
    ent=subprocess.run(['codesign','-d','--entitlements',':-',str(original)],capture_output=True,check=True).stdout
    entitlements=plistlib.loads(ent);out.mkdir(parents=True)
    ep=out/'entitlements.plist';ep.write_bytes(plistlib.dumps(entitlements,fmt=plistlib.FMT_XML))
    agent=out/'iconservicesagent';agent.write_bytes(unsigned)
    subprocess.run(['codesign','--force','--sign','-','--identifier',identifier,'--entitlements',str(ep),str(agent)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(agent)],check=True)
    actual=subprocess.run(['codesign','-d','--entitlements',':-',str(agent)],capture_output=True,check=True).stdout
    if plistlib.loads(actual)!=entitlements:raise RuntimeError('Entitlements changed')
    signed=agent.read_bytes()
    if any(sha(signed[v['offset']:v['offset']+v['bytes']])!=v['sha256'] for v in sections.values()):raise RuntimeError('Original text section changed')
    header='#pragma once\n'
    for name,data in [('elden_icon_cpu_payload',lib),('elden_icon_agent_payload',signed)]:
        header+='static const unsigned char '+name+'[] = {\n'+'\n'.join(','.join('0x%02x'%x for x in data[i:i+16])+',' for i in range(0,len(data),16))+'\n};\n'
    dest=out/'EldenIconCpuPayload.h';dest.write_bytes(header.encode())
    result={'original_sha256':sha(b),'library_sha256':sha(lib),'signed_agent_sha256':sha(signed),'header_sha256':sha(dest.read_bytes()),
            'header_byte_identical_to_v91':sha(dest.read_bytes())==HEADER,'original_text_sections_unchanged':True,'entitlements_unchanged':True,'device_operation':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    if not result['header_byte_identical_to_v91']:raise SystemExit('Generated header differs; retain output for audit and do not deploy')

if __name__=='__main__':main()
