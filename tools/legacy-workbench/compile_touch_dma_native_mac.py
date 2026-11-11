"""Build only the offline DMA allocation/cache adapter, no kernel mutation."""
import argparse,hashlib,json,struct,subprocess,sys
from pathlib import Path
ROOT=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==ROOT
SOURCE=ROOT/'research/touch-dma-native-20261109/source-working'
parser=argparse.ArgumentParser();parser.add_argument('--attempt',type=int,required=True);args=parser.parse_args()
assert 1<=args.attempt<=20
OUT=ROOT/f'out/touch-dma-native-20261109-v{args.attempt}';OUT.mkdir(exist_ok=False)
LLVM=Path('/Users/ciscohe/toolchains/llvm-20/bin')
sys.path.insert(0,str(ROOT/'third_party/UniXNU/kits/XnuPrelink'))
from native_prelink import macho,exports,backed_offset
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,cwd=ROOT):
 r=subprocess.run([str(x) for x in args],cwd=cwd,capture_output=True)
 if r.returncode:
  print(r.stdout.decode(errors='replace'));print(r.stderr.decode(errors='replace'));raise RuntimeError(str(args[0]))
 return r.stdout.decode()
base=ROOT/'out/adsp-pmic-ucsi45-20261108/EldenPmicClient/build.json'
ctx=json.loads(base.read_text());assert ctx['arm64_ios17']
cc=next(c for c in ctx['commands'] if any(x.endswith('/ChargePoll.c') for x in c))
cpp=next(c for c in ctx['commands'] if any(x.endswith('/PmicClient.cpp') for x in c))
tree=ROOT/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
cwd=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))['directory']
commands=[];objects=[];symbols={}
for name in ('GeniSpi.c','DmaTransfer.c','DmaCache.c','NativeDmaBuffer.cpp'):
 command=list(cpp if name.endswith('.cpp') else cc)
 idx=[i for i,x in enumerate(command) if x.endswith(('.c','.cpp')) and not x.startswith('-')];assert len(idx)==1
 obj=OUT/(Path(name).stem+'.o');command[idx[0]]=str(SOURCE/name);command[command.index('-o')+1]=str(obj)
 command+=['-I',str(SOURCE)];run(command,cwd);commands.append(command);objects.append(obj)
 raw=obj.read_bytes();m=macho(raw);assert struct.unpack_from('<II',raw)==(0xfeedfacf,0x100000c)
 assert not any(s['name']=='__mod_init_func' and s['size'] for g in m['segments'] for s in g['sections'])
 p=next(p for c,p,_ in m['commands'] if c==0x32);assert struct.unpack_from('<II',raw,p+8)==(2,17<<16)
 symbols[obj.name]=run([LLVM/'llvm-nm','-u',obj])
original=ROOT/'out/startup-services-20261107/kernel.macho'
assert sha(original)=='b6e349ed37705133d5e3ff7d9d09ec3a5fd89b4fb92cff0b2a1a4e83f5ae9247'
raw=original.read_bytes();names={n:v for v,ns in exports(raw,macho(raw)).items() for n in ns}
native=symbols['NativeDmaBuffer.o'];undefined={line.split()[-1] for line in native.splitlines() if line.strip()}
expected={
 '__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm',
 '__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv',
 '__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj',
 '__ZN25IOGeneralMemoryDescriptor7prepareEj',
 '__ZN25IOGeneralMemoryDescriptor8completeEj'}
vtables={'__ZTV25IOGeneralMemoryDescriptor','__ZTV24IOBufferMemoryDescriptor'}
assert vtables<=undefined and '__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm' in undefined,undefined
assert all(n in names for n in expected)
assert all(n in names or n=='_elden_dma_cache_span' for n in undefined),undefined-set(names)-{'_elden_dma_cache_span'}
assembly=run([LLVM/'llvm-objdump','--disassemble','--reloc',objects[-1]])
(OUT/'NativeDmaBuffer.disassembly.txt').write_text(assembly)
assert all(x in assembly for x in ('dc\tcvac','dc\tcivac','dc\tivac','dsb\tsy'))
slots=[('__ZTV25IOGeneralMemoryDescriptor',0xe8,'__ZN25IOGeneralMemoryDescriptor7prepareEj'),
       ('__ZTV25IOGeneralMemoryDescriptor',0xf0,'__ZN25IOGeneralMemoryDescriptor8completeEj'),
       ('__ZTV25IOGeneralMemoryDescriptor',0xa8,'__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj'),
       ('__ZTV24IOBufferMemoryDescriptor',0x158,'__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv'),
       ('__ZTV24IOBufferMemoryDescriptor',0x38,'__ZNK8OSObject7releaseEv')]
image=macho(raw)
for table,offset,symbol in slots:
 at=backed_offset(image['segments'],names[table]+offset,8,len(raw))
 assert struct.unpack_from('<Q',raw,at)[0]==names[symbol],(table,offset,symbol)
# Check the generated base-table loads, independently of header declarations.
assert all(f'#0x{x:x}]' in assembly for x in (0xe8,0xf0,0xa8,0x158,0x28))
archive=OUT/'libEldenTouchDmaNative.a';run([LLVM/'llvm-ar','rcsD',archive,*objects])
report=dict(archive_sha256=sha(archive),archive_bytes=archive.stat().st_size,
 commands=commands,source_files={p.name:sha(p) for p in SOURCE.iterdir() if p.is_file()},
 objects={p.name:sha(p) for p in objects},undefined=symbols,
 original_candidate_sha256=sha(original),original_native_bindings={n:hex(names[n]) for n in sorted(expected)},
 original_vtable_slots=slots,
 inherited_context_sha256=sha(base),arm64_ios17=True,automatic_initializers=False,
 runtime_linked=False,hardware_tested=False)
(OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ('archive_sha256','archive_bytes','runtime_linked','hardware_tested')},indent=2))
