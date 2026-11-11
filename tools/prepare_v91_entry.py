#!/usr/bin/env python3
"""Apply only the v91 blue entry probe and its already-qualified AMFI word."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Run without -O; validation assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kernel',type=Path,required=True);p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--clang',default='clang');a=p.parse_args()
    data=a.kernel.read_bytes();plan=json.loads(a.plan.read_text());out=a.output.resolve()
    if sha(data)!=plan['candidate_sha256']:raise RuntimeError('Prelinked kernel/plan identity mismatch')
    if plan['original_executable_sha256']['com.apple.iokit.IOMobileGraphicsFamily']!='2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b':raise RuntimeError('Wrong original display component')
    amfi=maker.locate(data,0xfffffff004aaecfc)[1]
    if struct.unpack_from('<I',data,amfi)[0]!=0xaa1503e0:raise RuntimeError('Unexpected original AMFI word')
    if out.exists():raise RuntimeError('Output must be new')
    out.mkdir(parents=True);source=out/'entry.s';obj=out/'entry.o';source.write_text(maker.probe_source(band=True,reset=False,delay=2))
    subprocess.run([a.clang,'--target=aarch64-none-elf','-c',str(source),'-o',str(obj)],check=True)
    stub=maker.elf_stub_text(obj.read_bytes());parsed=parse_kernel(data);probed,facts=maker.add_entry_probe(data,parsed,stub)
    result,_=maker.patch_word_at(probed,0xfffffff004aaecfc,0x17ffffd4,executable=False)
    segments=maker.segments(result);before=maker.segments(data)
    if segments[:-1]!=before or segments[-1]['name']!='__ELDEN_PROBE' or segments[-1]['file_size']!=104 or segments[-1]['protection']!=5:raise RuntimeError('Entry layout differs from v91')
    _,cmdbytes=struct.unpack_from('<II',data,16);entry=parsed['entry_file_offset']
    allowed=[(16,24),(32+cmdbytes,32+cmdbytes+72),(entry,entry+4),(amfi,amfi+4)]
    changed=[i for i,(x,y) in enumerate(zip(data,result)) if x!=y]
    if any(not any(lo<=i<hi for lo,hi in allowed) for i in changed):raise RuntimeError('Change outside qualified entry/AMFI scope')
    parse_kernel(result);dest=out/'kernel.macho';dest.write_bytes(result)
    record={'source_sha256':sha(data),'derivative_sha256':sha(result),'allowed_ranges':allowed,'changed_existing_bytes':len(changed),'stub_bytes':104,'entry':facts,'amfi':{'address':'0xfffffff004aaecfc','before':'0xaa1503e0','after':'0x17ffffd4'},'device_operation':False,'boot_packaged':False}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:record[k] for k in ('source_sha256','derivative_sha256','changed_existing_bytes','stub_bytes')},indent=2))
if __name__=='__main__':main()
