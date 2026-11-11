from pathlib import Path
import bisect,hashlib,json
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];src=r/'research/iconservices-cpu-20261111';out=r/'research/icon-gl-retry-20261111';m=json.loads((src/'manifest.json').read_text());im=next(x for x in m['images'] if x['path'].endswith('/CoreUI'))
b=(src/'CoreUI.text.bin').read_bytes();assert hashlib.sha256(b).hexdigest()==im['sha256']
s=sorted((int(x['address'],16),x['name']) for x in json.loads((src/'CoreUI-symbols.json').read_text()));starts=sorted(set(a for a,n in s));names={a:n for a,n in s}
selected=['-[CUIShapeEffectStack applyCustomForegroundColor:tintEffectColors:]','-[CUIColor CGColor]','-[_CUIGraphicVariantVectorGlyph _createContentEffectsForTargetSize:scale:]','-[_CUIGraphicVariantVectorGlyph _createShapeEffectsForTargetSize:scale:]','-[_CUIGraphicVariantVectorGlyph _initWithBaseGlyph:options:]','-[_CUIGraphicVariantVectorGlyph drawInContext:]','-[_CUIGraphicVariantVectorGlyph rasterizeImageUsingScaleFactor:forTargetSize:]','-[_CUIGraphicVariantVectorGlyph _drawBackgroundShapeInContext:targetSize:scale:]','-[_CUIGraphicVariantVectorGlyph _createBackgroundImageOfSize:scale:]']
records=[]
for n in selected:
 a=next(a for a,name in s if name==n);end=starts[bisect.bisect_right(starts,a)];data=b[a-im['base']:end-im['base']]
 lines=[];calls=[]
 for i in Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(data,a):
  target=names.get(int(i.op_str[1:],16),'') if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x') else ''
  if target:calls.append(target)
  lines.append(f'{i.address:x} {i.bytes.hex()} {i.mnemonic} {i.op_str}'+(' ; '+target if target else ''))
 (out/(hex(a)+'.asm')).write_text('\n'.join(lines)+'\n');records.append({'name':n,'address':hex(a),'sha256':hashlib.sha256(data).hexdigest(),'calls':calls})
(out/'vector-methods.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(records,indent=2))
