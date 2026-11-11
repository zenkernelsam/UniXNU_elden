"""Build an inherited-config AW99706 driver against the exact original 21H461 ABI."""
import argparse,hashlib,json,struct,subprocess,sys
from pathlib import Path
ROOT=Path.cwd().resolve();assert ROOT==Path('/Users/ciscohe/Desktop/UniXNU_elden')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--attempt',type=int,required=True);a=p.parse_args()
assert 1<=a.attempt<=99
SOURCE=ROOT/f'research/backlight-elden-20261109/source-input-v{a.attempt}'
OUT=ROOT/f'out/backlight-runtime-v{a.attempt}-20261109';OUT.mkdir(exist_ok=True)
LLVM=Path('/Users/ciscohe/toolchains/llvm-20/bin')
sys.path.insert(0,str(ROOT/'third_party/UniXNU/kits/XnuPrelink'))
from native_prelink import macho,backed_offset,exports
def sha(b):return hashlib.sha256(b).hexdigest()
def run(args,cwd=None):
 r=subprocess.run([str(v) for v in args],cwd=cwd,capture_output=True)
 if r.returncode:
  print(r.stdout.decode(errors='replace'));print(r.stderr.decode(errors='replace'));raise RuntimeError(str(args[0]))
 return r.stdout
def require(ok,why):
 if not ok:raise RuntimeError(why)
original=(ROOT/'out/startup-services-20261107/kernel.macho').read_bytes()
plan=json.loads((ROOT/'out/startup-services-20261107/kernel.json').read_text())
require(sha(original)==plan['candidate_sha256']=='b6e349ed37705133d5e3ff7d9d09ec3a5fd89b4fb92cff0b2a1a4e83f5ae9247','Pinned original ABI carrier')
image=macho(original);names={n:a for a,ns in exports(original,image).items() for n in ns}
def read(a,n):
 at=backed_offset(image['segments'],a,n,len(original));return original[at:at+n]
def pointers(a,n):return struct.unpack('<'+str(n)+'Q',read(a,n*8))
owned_module=next(e for e in plan['extents'] if e['segment']=='__TEXT_EXEC' and e['va']<=0xfffffff0049abc1c<e['va']+e['size'])
require(sha(read(owned_module['va'],owned_module['size']))==plan['original_executable_sha256'][owned_module['module']],'Complete original backlight instructions')
require(read(0xfffffff0049abc1c,52).hex()=='fd7bbfa9fd030091e14900940899009008613491080000f91f6000f91f8008391f9401f91fb001f91f6803b9fd7bc1a8c0035fd6','Backlight constructor storage writes')
require(read(0xfffffff0049abc84,4).hex()=='cc490014','Original protected destructor')
require(pointers(0xfffffff005cba2b8,2)==(names['__ZN9IOServiceC2EPK11OSMetaClass'],names['__ZN9IOServiceD2Ev']),'Actual native constructor/destructor imports')
require(read(0xfffffff0049be3a8,24).hex()=='f0970090105e41f900021fd6f0970090106241f900021fd6','Original base thunks')
require(pointers(0xfffffff005cba220,1)[0]==names['__ZN9IOService10gMetaClassE'] and read(0xfffffff004291985,18)==b'AppleARMBacklight\0','Original metaclass ancestry/name')
require(read(0xfffffff0049b52d8,4)==struct.pack('<I',0x52806e03),'Original 0x370 allocation')
require(all(pointers(0xfffffff005ccbd18,169)) and read(0xfffffff005ccbd18+169*8,24)==bytes(24),'Exact 169-slot original extent')
base=pointers(names['__ZTV9IOService']+16,168)
(OUT/'OriginalBacklightABI.h').write_text('''// SPDX-License-Identifier: BSD-2-Clause-Patent
// Generated after exact original-image qualification, never a copied live vtable.
#pragma once
#include <IOKit/IOService.h>
class AppleARMBacklight : public IOService {
 OSDeclareAbstractStructors(AppleARMBacklight)
 unsigned char originalPrivate[0x370-sizeof(IOService)];
public:
 virtual void unqualifiedBacklightMethod()=0;
};
static_assert(sizeof(AppleARMBacklight)==0x370,"Original instance extent");
''')
ctx=ROOT/'out/adsp-pmic-ucsi45-20261108/EldenPmicClient'
context=json.loads((ctx/'build.json').read_text());require(context['arm64_ios17'],'Native iOS17 compiler context')
cpp=next(c for c in context['commands'] if any(x.endswith('/PmicClient.cpp') for x in c))
cc=next(c for c in context['commands'] if any(x.endswith('/ChargePoll.c') for x in c))
tree=ROOT/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
cwd=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))['directory']
commands=[];objects=[]
for file in ('GeniI2c.c','Aw99706.c','Platform.c','BacklightRuntime.cpp'):
 cmd=list(cpp if file.endswith('.cpp') else cc)
 inputs=[i for i,x in enumerate(cmd) if x.endswith(('.c','.cpp')) and not x.startswith('-')];require(len(inputs)==1,'One compiler input')
 cmd[inputs[0]]=str(SOURCE/file);obj=OUT/(Path(file).stem+'.o');cmd[cmd.index('-o')+1]=str(obj)
 cmd+=['-I',str(SOURCE),'-I',str(OUT)];commands.append(cmd);run(cmd,cwd);objects.append(obj)
raw=objects[-1].read_bytes();om=macho(raw)
st=next(p for c,p,_ in om['commands'] if c==2);sy,count,strings,_=struct.unpack_from('<4I',raw,st+8);symbols=[]
for i in range(count):
 ix,kind,section,_,va=struct.unpack_from('<IBBHQ',raw,sy+i*16)
 symbols.append((raw[strings+ix:raw.index(b'\0',strings+ix)].decode(),va,kind))
vt=next(v for n,v,_ in symbols if n=='__ZTV14EldenBacklight');relocs={}
for segment in om['segments']:
 for sec in segment['sections']:
  if sec['va']<=vt<sec['va']+sec['size']:
   ro,rc=struct.unpack_from('<2I',raw,sec['command_offset']+56)
   for i in range(rc):
    offset,bits=struct.unpack_from('<2I',raw,ro+i*8)
    if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocs[sec['va']+offset]=symbols[bits&0xffffff][0]
compiled=[relocs.get(vt+16+i*8) for i in range(169)]
require(all(compiled) and vt+16+169*8 not in relocs,'Compiled exact 169-slot extent')
own=[]
for i,n in enumerate(compiled):
 if n.startswith(('__ZN14EldenBacklight','__ZNK14EldenBacklight')):own.append(i)
 else:require(i<168 and names.get(n)==base[i],f'Actual inherited IOService slot {i}: {n}')
require(own==[0,1,7,14,21,44,86,87,111,168],f'Only reviewed lifecycle, setProperties and getWorkLoop overrides: {own}')
qualified={'__ZN17AppleARMBacklightC2EPK11OSMetaClass':0xfffffff0049abc1c,
 '__ZN17AppleARMBacklightD2Ev':0xfffffff0049abc84,'__ZN17AppleARMBacklight10gMetaClassE':0xfffffff006ade638}
undefined={n for n,_,k in symbols if k==1 and 'AppleARMBacklight' in n}
require(undefined==set(qualified),f'Actual original base imports: {undefined}')
asm=OUT/'original-bindings.S';asm.write_text(''.join(f'.globl {n}\n.set {n}, {v:#x}\n' for n,v in sorted(qualified.items())))
sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path']).decode().strip()
binding=OUT/'original-bindings.o';run(['xcrun','clang','-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',asm,'-o',binding])
late=OUT/'BacklightRuntime.late.o';run([LLVM/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_BacklightRuntime.cpp',objects[-1],late])
def code(b):
 m=macho(b);s=next(s for g in m['segments'] for s in g['sections'] if s['name']=='__text');return b[s['fileoff']:s['fileoff']+s['size']]
require(code(raw)==code(late.read_bytes()),'Deferred registration preserves actual instructions')
objects[-1]=late;objects.append(binding)
for obj in objects:
 m=macho(obj.read_bytes());require(not any(s['name']=='__mod_init_func' and s['size'] for g in m['segments'] for s in g['sections']),'No early static initialization')
archive=OUT/'libEldenBacklight.a';run([LLVM/'llvm-ar','rcsD',archive,*objects])
audit=dict(reference_candidate_sha256=sha(original),original_module=owned_module['module'],
 original_text_sha256=plan['original_executable_sha256'][owned_module['module']],original_slots=169,
 original_instance_size=0x370,owned_slots=own,bindings=qualified,class_initializer_deferred=True,runtime_qualified=False)
(OUT/'abi-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
report=dict(archive=str(archive),archive_sha256=sha(archive.read_bytes()),archive_bytes=archive.stat().st_size,
 objects={p.name:sha(p.read_bytes()) for p in objects},commands=commands,arm64_ios17=True,automatic_initializers=False,
 source_files={p.name:sha(p.read_bytes()) for p in SOURCE.iterdir() if p.is_file()},hardware_tested=False)
(OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(archive_sha256=report['archive_sha256'],bytes=report['archive_bytes'],slots=169,original_module=audit['original_module'],hardware_tested=False),indent=2))
