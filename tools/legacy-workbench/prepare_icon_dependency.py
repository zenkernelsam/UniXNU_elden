"""Prepare an undeployed normal-dylib-dependency clone; preserve original code."""
from pathlib import Path
import hashlib,json,plistlib,struct
r=Path(__file__).resolve().parents[1];src=r/'research/loader-policy-20261111/OS-iconservicesagent';b=src.read_bytes()
assert hashlib.sha256(b).hexdigest()=='dd01b74e4413e0bb72de4a62f3f7675deb5026cad79d376fde6570abd0d012f8'
out=r/'edl_work/icon-dependency-20261111';out.mkdir(exist_ok=True)
n,sz=struct.unpack_from('<II',b,16);at=32;code_sections={};sig=None;identifier=None;uuid_offset=None
for _ in range(n):
 cmd,size=struct.unpack_from('<II',b,at);assert size>=8 and at+size<=32+sz
 if cmd==0x19:
  seg=b[at+8:at+24].split(b'\0')[0].decode();count=struct.unpack_from('<I',b,at+64)[0]
  for i in range(count):
   s=at+72+i*80;name=b[s:s+16].split(b'\0')[0].decode();va,length,off=struct.unpack_from('<QQI',b,s+32)
   if seg=='__TEXT':code_sections[name]={'offset':off,'bytes':length,'sha256':hashlib.sha256(b[off:off+length]).hexdigest()}
 if cmd==0x1d:
  off,length=struct.unpack_from('<II',b,at+8);sig=at;blob=b[off:off+length];_,_,count=struct.unpack_from('>III',blob)
  for i in range(count):
   slot,pos=struct.unpack_from('>II',blob,12+i*8)
   if slot==0:
    ident=struct.unpack_from('>I',blob,pos+20)[0];start=pos+ident;identifier=blob[start:blob.index(b'\0',start)].decode()
 if cmd==0x1b:uuid_offset=at+8
 at+=size
path='/System/Library/CoreServices/EldenIconCpuMode.dylib'
size=(24+len(path)+1+7)&~7
command=struct.pack('<6I',0xc,size,24,0,0,0)+path.encode()+b'\0';command+=bytes(size-len(command))
start=32+sz;first=min(x['offset'] for x in code_sections.values() if x['offset']);assert start+size<=first and not any(b[start:first])
new=bytearray(b);struct.pack_into('<II',new,16,n+1,sz+size);new[start:start+size]=command
assert sig and identifier and uuid_offset
# Different dependency graph must not reuse a prebuilt loader keyed by UUID.
new_uuid=hashlib.sha256(b+path.encode()+b'elden-normal-dependency').digest()[:16]
new[uuid_offset:uuid_offset+16]=new_uuid
(out/'iconservicesagent.unsigned').write_bytes(new)
meta=json.loads((r/'research/loader-policy-20261111/original-inputs.json').read_text());ent=next(x['entitlements'] for x in meta if x.get('path')=='/System/Library/CoreServices/iconservicesagent')
(out/'entitlements.plist').write_bytes(plistlib.dumps(ent,fmt=plistlib.FMT_XML))
report=dict(original_sha256=hashlib.sha256(b).hexdigest(),unsigned_sha256=hashlib.sha256(new).hexdigest(),identifier=identifier,dependency=path,added_command_bytes=size,header_padding_bytes=first-start,uuid_offset=uuid_offset,new_uuid=new_uuid.hex(),code_sections=code_sections,entitlements=ent,device_deployed=False,hardware_tested=False)
(out/'prepare.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['identifier','dependency','added_command_bytes','header_padding_bytes','device_deployed']},indent=2))
