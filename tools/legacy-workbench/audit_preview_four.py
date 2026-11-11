"""Verify the real display-object delta is only the requested preview count."""
from pathlib import Path
import hashlib,json,re,struct
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];p=r/'research/preview-four-20261111'
def text_section(f):
 b=f.read_bytes();n=struct.unpack_from('<I',b,16)[0];at=32
 for _ in range(n):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for j in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+80*j
    if b[x:x+16].split(b'\0')[0]==b'__text':
     address,size,offset=struct.unpack_from('<QQI',b,x+32);return address,b[offset:offset+size]
  at+=length
 raise AssertionError('No text')
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);decoded=[]
for n in ('control.o','EldenDisplayGlue.o'):
 a,b=text_section(p/n);decoded.append(list(cs.disasm(b,a)))
before,after=decoded;assert len(before)==len(after)
diff=[(a,b) for a,b in zip(before,after) if a.bytes!=b.bytes];assert len(diff)==1
a,b=diff[0];assert a.mnemonic==b.mnemonic=='mov' and a.op_str.endswith(', #2') and b.op_str==a.op_str[:-1]+'4'
assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in after)
source=(p/'EldenDisplayGlue.cpp').read_text();assert 'previewCaptured>=10' in source
report=json.loads((p/'result.json').read_text());assert report['control_sections_equal'] and not report['kernel_linked']
assert hashlib.sha256((p/'EldenDisplayGlue.o').read_bytes()).hexdigest()==report['object_sha256']
result={'accepted_object':True,'instructions':len(after),'fp_simd':False,'changed_instructions':[{'address':hex(a.address),'before':a.mnemonic+' '+a.op_str,'after':b.mnemonic+' '+b.op_str}], 'per_boot_cap':10,'requests_per_input_before':2,'requests_per_input_after':4,'kernel_linked':False,'hardware_tested':False,'archive_sha256':report['archive_sha256'],'source_sha256':report['source_sha256']}
(p/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
