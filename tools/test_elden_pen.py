"""Test actual pen decoder/delivery and compile ARM64/iOS17 scalar objects.

Synthetic protocol frames are not captured SPI packets; the production send
callback is modeled here. No hardware access, HID acceptance or full link claim.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
ROOT=Path(__file__).resolve().parents[1]
DRIVER=ROOT/'drivers/EldenPen'
TOUCH=ROOT/'drivers/EldenNvt36536'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--llvm',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    results={}
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; logs retained')
        return r.stdout.decode('utf8','replace')
    sources=[DRIVER/n for n in ('PenReport.c','Delivery.c','TestPen.c')]+[TOUCH/'PointReport.c']
    exe=out/'pen-test'
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         *sources,'-o',exe],'host-build')
    result=run([exe],'host-run')
    clang=a.llvm/'clang';resource=run([clang,'-print-resource-dir'],'resource').strip()
    for source in sources[:2]:
        obj=out/(source.stem+'.o')
        cmd=[clang,'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin',
             '-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include',
             '-Wno-error=incompatible-sysroot','-Wall','-Wextra','-Werror','-c',source,'-o',obj]
        run(cmd,source.stem+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],source.stem+'-disassembly')
        instructions=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        # Match operands/instructions, not hex addresses such as d0 or d8.
        if not instructions or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in instructions):
            raise ValueError('Missing disassembly or unexpected FP/SIMD register')
        headers=run([a.llvm/'llvm-objdump','--macho','--private-headers',obj],source.stem+'-headers')
        if '__mod_init_func' in headers or 'ios' not in headers.lower():raise ValueError('Native metadata differs')
        imports=run([a.llvm/'llvm-nm','--undefined-only',obj],source.stem+'-imports')
        exports=run([a.llvm/'llvm-nm','--defined-only','--extern-only',obj],source.stem+'-exports')
        results[source.name]={'source_sha256':sha(source),'object_sha256':sha(obj),'imports':imports.splitlines(),
                              'exports':exports.splitlines(),'command':list(map(str,cmd))}
    report={'passed':True,'host_output':result,'sources':{p.name:sha(p) for p in sources+[DRIVER/'PenReport.h',DRIVER/'Delivery.h',TOUCH/'PointReport.h']},
            'native':results,'warnings':0,'actual_functions':True,'synthetic_packets':True,
            'native_hid_linked':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(result.strip())
if __name__=='__main__':main()
