"""Read exact 21H461 CoreUI method encodings from its original dyld cache."""
from pathlib import Path
import hashlib,json,plistlib,struct,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/graphic-variant-cpu-20261111';p.mkdir(exist_ok=True);mount=p/'mount';mount.mkdir(exist_ok=True);assert not any(mount.iterdir())
attached=plistlib.loads(subprocess.check_output(['hdiutil','attach','-readonly','-nobrowse','-owners','off','-imagekey','diskimage-class=CRawDiskImage','-mountpoint',mount,'-plist',r/'out/prerequisites-20261106/root-images/SystemOS.raw']))
dev=next(x['dev-entry'] for x in attached['system-entities'] if 'dev-entry' in x)
try:
 cache=mount/'System/Library/Caches/com.apple.dyld';maps=[]
 for f in cache.glob('dyld_shared_cache_arm64*'):
  if f.suffix=='.symbols':continue
  with f.open('rb') as stream:
   header=stream.read(32)
   if not header.startswith(b'dyld_v1   arm64'):continue
   off,count=struct.unpack_from('<II',header,16);assert 0<count<1024;stream.seek(off)
   for _ in range(count):
    va,size,fileoff,maxprot,prot=struct.unpack('<QQQII',stream.read(32));maps.append((f,va,size,fileoff))
 def read(va,n):
  for f,start,size,off in maps:
   if start<=va and va+n<=start+size:
    with f.open('rb') as stream:stream.seek(off+va-start);b=stream.read(n)
    assert len(b)==n;return b
  raise ValueError(hex(va))
 def string(va):return read(va,512).split(b'\0')[0].decode()
 rows=[]
 selected={0x1a318679c:'_createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:',0x1a3187744:'options',0x1a3187d04:'monochromeForegroundColor',0x1a31ac518:'CGColor'}
 for cls,va in [('_CUIGraphicVariantVectorGlyph',0x1a31f0108),('CUIVectorGlyphGraphicVariantOptions',0x1a31eff80),('CUIColor',0x1a31f1530)]:
  flags,count=struct.unpack('<II',read(va,8));assert 0<count<1024
  size=flags&0xfffc;assert size in (12,24),(hex(flags),size) # low two bits are list flags
  for i in range(count):
   e=va+8+i*size
   if flags&0x80000000:
    no,to,io=struct.unpack('<iii',read(e,12));na=e+no
    imp=e+8+io
    if imp not in selected:continue
    # Shared-cache direct selectors use a separate selector base. Select these
    # entries by the exact original method symbol/IMP; runtime still verifies
    # the named selector, IMP and type together before any replacement.
    name,types=selected[imp],string(e+4+to)
   else:
    na,ta,imp=struct.unpack('<QQQ',read(e,24));name,types=string(na),string(ta)
   if name in ('_createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:','options','monochromeForegroundColor','contentEffect','shapeEffect','CGColor'):
    rows.append({'class':cls,'selector':name,'types':types,'imp':hex(imp),'first16':read(imp,16).hex(),'selection':'original method symbol and method-list IMP; runtime selector confirmation required'})
 result={'source':'original 21H461 SystemOS dyld cache; readonly','methods':rows}
 (p/'abi.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
finally:subprocess.run(['hdiutil','detach',dev],check=True)
