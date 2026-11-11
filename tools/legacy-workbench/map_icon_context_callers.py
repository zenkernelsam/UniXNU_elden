"""Index real 21H461 calls into CoreImage/context creation; no runtime edits."""
from pathlib import Path
import bisect,hashlib,json,re
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];src=r/'research/iconservices-cpu-20261111';out=r/'research/icon-gl-retry-20261111';out.mkdir(exist_ok=True)
manifest=json.loads((src/'manifest.json').read_text());records=[]
for item in manifest['images']:
 name=item['path'].split('/')[-1];data=(src/(name+'.text.bin')).read_bytes();assert hashlib.sha256(data).hexdigest()==item['sha256']
 symbols=json.loads((src/(name+'-symbols.json')).read_text());syms=sorted((int(s['address'],16),s['name']) for s in symbols);starts=sorted(set(a for a,n in syms));names={}
 for a,n in syms:names.setdefault(a,[]).append(n)
 targets={a:n for a,n in syms if re.search(r'(sharedCIContext|contextWithOptions:|internalGLContextWithOptions:|initWithOptions:|CIContext|CoreImage|ciContext)',n)}
 cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True
 for ins in cs.disasm(data,item['base']):
  if ins.mnemonic not in ('bl','b') or not ins.op_str.startswith('#0x'):continue
  target=int(ins.op_str[1:],16)
  if target not in targets:continue
  i=bisect.bisect_right(starts,ins.address)-1
  if i<0:continue
  start=starts[i];end=starts[i+1] if i+1<len(starts) else item['base']+len(data)
  if end-start>65536:continue
  row={'image':name,'caller':names[start],'caller_start':hex(start),'call_address':hex(ins.address),'target':targets[target],'target_address':hex(target)};records.append(row)
  filename=out/(name+'-'+hex(start)+'.asm')
  if not filename.exists():
   code=data[start-item['base']:end-item['base']]
   text=[]
   for x in cs.disasm(code,start):
    targetnames=names.get(int(x.op_str[1:],16),[]) if x.mnemonic in ('bl','b') and x.op_str.startswith('#0x') else []
    text.append(f'{x.address:x} {x.bytes.hex()} {x.mnemonic} {x.op_str}'+(' ; '+' | '.join(targetnames) if targetnames else ''))
   filename.write_text('\n'.join(text)+'\n')
report={'cache_uuid':manifest['cache_uuid'],'manifest_sha256':hashlib.sha256((src/'manifest.json').read_bytes()).hexdigest(),'calls':records,'hardware_path_proven':False}
(out/'context-callers.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(records,indent=2))
