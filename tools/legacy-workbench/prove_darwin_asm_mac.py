from pathlib import Path
import subprocess,json,re,hashlib
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-asm-separators-20261111';p.mkdir(exist_ok=True)
source=r'''typedef unsigned long long u64;
void bad_sgi(u64 x){__asm__ volatile("dsb ishst; msr icc_sgi1r_el1,%0; isb"::"r"(x):"memory");}
void good_sgi(u64 x){__asm__ volatile("dsb ishst\n\tmsr icc_sgi1r_el1,%0\n\tisb"::"r"(x):"memory");}
void bad_store(u64 p,unsigned x){__asm__ volatile("dsb sy; str %w0,[%1]; dsb sy"::"r"(x),"r"(p):"memory");}
void good_store(u64 p,unsigned x){__asm__ volatile("dsb sy\n\tstr %w0,[%1]\n\tdsb sy"::"r"(x),"r"(p):"memory");}
'''
f=p/'separators.c';f.write_text(source)
clang='/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
subprocess.run([clang,'-target','arm64-apple-ios17','-O2','-ffreestanding','-mgeneral-regs-only','-c',f,'-o',p/'separators.o'],check=True)
objdump='/Users/ciscohe/toolchains/llvm-20/bin/llvm-objdump';rows={}
for name in ('bad_sgi','good_sgi','bad_store','good_store'):
 text=subprocess.check_output([objdump,'--disassemble-symbols=_'+name,p/'separators.o'],text=True);(p/(name+'.asm')).write_text(text)
 rows[name]={'msr_sgi':len(re.findall(r'\bmsr\s+ICC_SGI1R_EL1',text,re.I)),'str':len(re.findall(r'\bstr\s',text))}
assert rows['bad_sgi']['msr_sgi']==0 and rows['good_sgi']['msr_sgi']==1
assert rows['bad_store']['str']==0 and rows['good_store']['str']==1
(p/'proof.json').write_text(json.dumps({'source_sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'compiler':clang,'results':rows,'executed_on_device':False},indent=2)+'\n');print(json.dumps(rows,indent=2))
