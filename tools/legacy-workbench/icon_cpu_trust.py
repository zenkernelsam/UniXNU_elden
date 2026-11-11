"""Append one exact ad-hoc CodeDirectory to the existing trust-cache container."""
import hashlib,struct

def cdhash(data):
 assert data[:4]==bytes.fromhex('cffaedfe')
 n,cmdbytes=struct.unpack_from('<II',data,16);at=32;found=[]
 for _ in range(n):
  cmd,size=struct.unpack_from('<II',data,at);assert size>=8 and at+size<=32+cmdbytes
  if cmd==0x1d:
   off,length=struct.unpack_from('<II',data,at+8);assert off+length<=len(data)
   blob=data[off:off+length];magic,total,count=struct.unpack_from('>III',blob);assert magic==0xfade0cc0 and total<=len(blob)
   for i in range(count):
    slot,pos=struct.unpack_from('>II',blob,12+i*8);m,l=struct.unpack_from('>II',blob,pos)
    if m==0xfade0c02 and slot in (0,0x1000,0x1001):
     assert pos+l<=total;cd=blob[pos:pos+l]
     if cd[37]==2:assert cd[36]==32;found.append(hashlib.sha256(cd).digest()[:20])
  at+=size
 assert len(found)==1 and at==32+cmdbytes
 return found[0]

def parts(container):
 count=struct.unpack_from('<I',container)[0];assert 1<=count<=32
 offsets=list(struct.unpack_from('<'+str(count)+'I',container,4))+[len(container)]
 assert offsets[0]==4*(count+1) and offsets==sorted(set(offsets))
 result=[]
 for start,end in zip(offsets,offsets[1:]):
  part=container[start:end];version=struct.unpack_from('<I',part)[0];n=struct.unpack_from('<I',part,20)[0]
  assert version==2 and len(part)==24+n*24
  result.append(part)
 return result

def append(original,library):
 old=parts(original);digest=cdhash(library)
 assert not any(p[i:i+20]==digest for p in old for i in range(24,len(p),24))
 uid=hashlib.sha256(original+digest).digest()[:16]
 part=struct.pack('<I16sI',2,uid,1)+digest+bytes([2,0,0,0]);allparts=old+[part]
 offsets=[];at=4*(len(allparts)+1)
 for p in allparts:offsets.append(at);at+=len(p)
 result=struct.pack('<I',len(allparts))+struct.pack('<'+str(len(offsets))+'I',*offsets)+b''.join(allparts)
 assert parts(result)[:-1]==old and parts(result)[-1]==part
 return result,digest.hex()

if __name__=='__main__':
 from pathlib import Path
 import json
 d=Path(__file__).resolve().parent/'icon-cpu-20261111';original=(d/'original.trustcache').read_bytes();library=(d/'EldenIconCpuMode.dylib').read_bytes()
 result,h=append(original,library);(d/'kernel.macho.trustcache').write_bytes(result)
 report=dict(original_sha256=hashlib.sha256(original).hexdigest(),library_sha256=hashlib.sha256(library).hexdigest(),cdhash=h,old_parts=len(parts(original)),new_parts=len(parts(result)),original_parts_unchanged=True,new_sha256=hashlib.sha256(result).hexdigest(),bytes=len(result),hardware_tested=False)
 (d/'trustcache.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
