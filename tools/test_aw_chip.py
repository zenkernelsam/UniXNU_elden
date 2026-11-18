"""ASan/UBSan the extracted Gen4 driver through real adapter callbacks; compile
its native scalar object. Hardware/bus/calibration are explicit modeled peers.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','firmware','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text());spec=record['upstream']['firmware'];raw=a.firmware.read_bytes()
    if len(raw)!=spec['bytes'] or hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('Original firmware differs')
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; logs retained')
        return r.stdout.decode()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    run(['xcrun','clang',*flags,a.source/'AwChip.c',ROOT/'drivers/EldenHaptics/TestAwChip.c','-o',out/'test'],'host-build')
    message=run([out/'test',a.firmware],'host-run')
    clang=a.llvm/'clang';resource=run([clang,'-print-resource-dir'],'resource').strip();obj=out/'AwChip.o'
    cmd=[clang,'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-I',a.source,'-c',a.source/'AwChip.c','-o',obj]
    run(cmd,'native-build');dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-disassembly')
    instructions=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not instructions or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in instructions):raise ValueError('Unexpected SIMD/FP')
    headers=run([a.llvm/'llvm-objdump','--macho','--private-headers',obj],'native-headers')
    if '__mod_init_func' in headers or 'ios' not in headers.lower():raise ValueError('Wrong native metadata')
    imports=run([a.llvm/'llvm-nm','--undefined-only',obj],'native-imports')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'source_sha256':hashlib.sha256((a.source/'AwChip.c').read_bytes()).hexdigest(),
        'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'imports':imports.splitlines(),'native_command':list(map(str,cmd)),
        'source_record_sha256':hashlib.sha256((a.source/'source.json').read_bytes()).hexdigest(),'firmware_sha256':spec['sha256'],
        'actual_upstream_bodies':True,'native_bus_connected':False,'mechanical_calibration':False,'hardware':False},indent=2)+'\n')
    print(message)
if __name__=='__main__':main()
