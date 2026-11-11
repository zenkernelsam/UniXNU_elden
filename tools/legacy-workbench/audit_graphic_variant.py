"""Bind v90's real library, method ABI, kernel payload and independent markers."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[90,91,92],default=90);version=a.parse_args().version
r=Path(__file__).resolve().parents[1];v=r/f'edl_work/basic-bundle-20261108/v{version}';src=r/f'research/basic-bundle-20261108/source-compiled-v{version}';old=r/'research/basic-bundle-20261108/source-compiled-v89';ref=r/f'edl_work/icon-dependency-20261111/v{version}';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
lb=json.loads((ref/'library-build.json').read_text());library=(ref/'EldenIconCpuMode.dylib').read_bytes();kernel=(v/'final/kernel.macho').read_bytes()
assert sha(ref/'EldenIconCpuMode.dylib')==lb['dylib_sha256'] and kernel.count(library)==1
assert sha(src/'package/EldenIconCpuMode.c')==lb['source_sha256'] and sha(src/'package/EldenIconCpuPaths.h')==lb['paths_sha256']
for n,h in lb['extra_sources'].items():assert sha(src/'package'/n)==h
core=json.loads((r/'research/graphic-variant-cpu-20261111/core-build.json').read_text());assert core['host_passed']
for n in ('EldenGraphicVariantCpu.c','EldenGraphicVariantCpu.h'):assert core['sources'][n]==sha(src/'package'/n)
hook=(src/'package/EldenGraphicVariantHook.c').read_text();abi=json.loads((r/'research/graphic-variant-cpu-20261111/abi.json').read_text())
for row in abi['methods']:
 assert row['selector'] in hook and row['types'] in hook and row['imp'] in hook
for label,selector,size in [('v','_createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:',16),('o','options',16),('f','monochromeForegroundColor',8),('c','CGColor',8)]:
 match=re.search(r'static const uint8_t '+label+r'\[\]=\{([^}]+)\}',hook);assert match
 actual=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',match[1]));row=next(x for x in abi['methods'] if x['selector']==selector)
 assert actual==bytes.fromhex(row['first16'])[:size]
assert 'strcmp(process,"iconservicesagent")' in hook and 'strcmp(enabled,"0")' in hook
assert 'memcmp(((const struct uuid_command *)lc)->uuid,coreuiUUID,16)' in hook
assert 'method_getImplementation(m)!=colorImplementation' in hook
if version in (91,92):
 assert 'in_class(color,cfColorClass)&&CFGetTypeID((CFTypeRef)color)==CGColorGetTypeID()' in hook
 assert 'cfColorClass=object_getClass((id)reference)' in hook
 assert b'PASS reference class' in (r/'research/graphic-variant-cpu-20261111/color-host.log').read_bytes()
assert 'originalVariant(self,command' in hook and 'elden_graphic_variant_cpu(background,symbol,destination,sourceScale,outputScale,tint,disableColorFill,&error)' in hook
before=(old/'xnu/EldenIconCpuCommon.inc').read_text();after=(src/'xnu/EldenIconCpuCommon.inc').read_text()
begin=after.index('    if(code==ELDEN_ICON_GLYPH_TRACE_CODE){');end=after.index('    static unsigned reported,rejected;',begin)
assert after[:begin]+after[end:]==before
for text in ('status<1||status>4','version!=1','!=276','>64','ELDEN_GLYPH pid='):assert text in after[begin:end]
build=json.loads((v/'final/cpu-irq-snapshot-result.json').read_text());delta=build['glyph_delta'];assert delta['original_object_reproduced'] and delta['control_link_reproduced']
assert sha(src/'xnu/kern_exec.c')==delta['source']['parent_exec_sha256'] and sha(old/'xnu/EldenIconCpuCommon.inc')==delta['source']['parent_common_sha256']
for n,h in delta['source']['files'].items():assert sha(src/'xnu'/n)==h
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
def inspect(data,names,fp_allowed):
 s=m.symbols(data);starts=sorted(set(s.values()));reverse={a:n for n,a in s.items()};rows={}
 for n in names:
  a=s[n];end=next(x for x in starts if x>a);off=m.locate(data,a)[1];ins=list(Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(data[off:off+end-a],a));assert ins
  fp=any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins);assert fp_allowed or not fp
  calls=[reverse.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')]
  rows[n]={'address':hex(a),'instructions':len(ins),'fp_simd':fp,'calls':calls}
  (v/(n+'.asm')).write_text('\n'.join(f'{i.address:x} {i.mnemonic} {i.op_str}' for i in ins)+'\n')
 return rows
user=inspect(library,['_install','_elden_install_graphic_variant_cpu','_cpu_variant','_elden_graphic_variant_cpu'],True)
assert '_elden_install_graphic_variant_cpu' in user['_install']['calls'] and '_elden_graphic_variant_cpu' in user['_cpu_variant']['calls']
native=inspect(kernel,['_elden_icon_cpu_user_marker'],False)
assembly=(v/'_elden_icon_cpu_user_marker.asm').read_text();assert '#0xfe70' in assembly and '#0x14' in assembly
assert b'ELDEN_GLYPH pid=' in kernel
report={'accepted':True,'kernel_sha256':hashlib.sha256(kernel).hexdigest(),'library_sha256':lb['dylib_sha256'],'coreui_abi_sha256':sha(r/'research/graphic-variant-cpu-20261111/abi.json'),'sources':{str(p.relative_to(src)):sha(p) for p in [src/'package/EldenGraphicVariantHook.c',src/'package/EldenGraphicVariantCpu.c',src/'xnu/EldenIconCpuCommon.inc']},'library_functions':user,'kernel_functions':native,'marker_no_simd':True,'actual_cpu_renderer_reachable':True,'extra_CI_effects_implemented':False,'hardware_tested':False}
(v/'glyph-codegen.json').write_text(json.dumps(report,indent=2)+'\n');print({'accepted':True,'library_bytes':len(library),'kernel_marker_no_fp':True})
