"""Run the real GENI/AW composition and native ARM64 object checks. No device IO."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','firmware','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Generated source changed')
    spec=record['upstream']['firmware'];raw=a.firmware.read_bytes()
    if len(raw)!=spec['bytes'] or sha(a.firmware)!=spec['sha256']:raise ValueError('Firmware identity differs')
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; logs retained')
        return r.stdout.decode()
    files=[a.source/n for n in ('GeniCore.c','Bus.c','AwChip.c')]
    cmd=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,*files,ROOT/'drivers/EldenHaptics/TestBus.c','-o',out/'test']
    run(cmd,'host-build');message=run([out/'test',a.firmware],'host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();records={}
    for src in files:
        obj=out/(src.stem+'.o')
        cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
             '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',src,'-o',obj]
        run(cmd,src.stem+'-compile');dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],src.stem+'-asm')
        instructions=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not instructions or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in instructions):raise ValueError('Unexpected FP/SIMD')
        headers=run([a.llvm/'llvm-objdump','--macho','--private-headers',obj],src.stem+'-headers')
        if '__mod_init_func' in headers or 'ios' not in headers.lower():raise ValueError('Invalid native metadata')
        imports=run([a.llvm/'llvm-nm','--undefined-only',obj],src.stem+'-imports')
        records[src.name]={'source_sha256':sha(src),'object_sha256':sha(obj),'imports':imports.splitlines(),'command':list(map(str,cmd))}
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'objects':records,
        'source_record_sha256':sha(a.source/'source.json'),'actual_geni_aw_composition':True,
        'mmio_modeled':True,'native_platform_connected':False,'hardware':False},indent=2)+'\n')
    print(message)
if __name__=='__main__':main()
