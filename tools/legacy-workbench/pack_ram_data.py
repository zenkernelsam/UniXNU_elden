"""Mac: pack the fresh Data seed as the existing DarwinBoot DBRDISK1 format."""
from pathlib import Path
import hashlib,json,struct
root=Path.cwd();out=root/'out/ram-data-20261107';raw=out/'elden-data-512.cdr'
report=json.loads((out/'seed.json').read_text());extents=[];sha=hashlib.sha256();at=0
with raw.open('rb') as f:
 while page:=f.read(4096):
  assert len(page)==4096;sha.update(page)
  if page!=bytes(4096):
   if extents and extents[-1][0]+extents[-1][1]==at:extents[-1][1]+=4096
   else:extents.append([at,4096])
  at+=4096
assert at==report['bytes']==536870912 and sha.hexdigest()==report['sha256'] and len(extents)<=65536
packed=out/'data.packed'
with packed.open('wb') as dest,raw.open('rb') as src:
 dest.write(struct.pack('<8sIIQQ',b'DBRDISK1',1,4096,at,len(extents)))
 for off,n in extents:dest.write(struct.pack('<QQ',off,n))
 for off,n in extents:
  src.seek(off)
  while n:
   b=src.read(min(n,1<<20));assert b;dest.write(b);n-=len(b)
assert packed.stat().st_size<256*(1<<20)
# Independent streaming reconstruction verifies real zero holes and all bytes.
b=packed.read_bytes();count=struct.unpack_from('<Q',b,24)[0];cursor=32+count*16;end=0;rebuilt=hashlib.sha256();zeros=bytes(1<<20)
for i in range(count):
 off,n=struct.unpack_from('<QQ',b,32+i*16);assert off>=end and (off|n)%4096==0 and off+n<=at
 gap=off-end
 while gap:take=min(gap,len(zeros));rebuilt.update(zeros[:take]);gap-=take
 rebuilt.update(b[cursor:cursor+n]);cursor+=n;end=off+n
while end<at:take=min(at-end,len(zeros));rebuilt.update(zeros[:take]);end+=take
assert cursor==len(b) and rebuilt.hexdigest()==report['sha256']
result=dict(packed_bytes=len(b),packed_sha256=hashlib.sha256(b).hexdigest(),expanded_bytes=at,expanded_sha256=report['sha256'],extent_count=count,reconstructed_sha_matches=True)
(out/'packed.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
