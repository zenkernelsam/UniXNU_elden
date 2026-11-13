"""Independently check the one-object RT memory change and prelinked helper."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess
from audit_entry_relocation import symbol
from audit_hid_kernel import at_va
from replay_xnu_objects import global_symbols
from link_v91_kernel import verify_original_executables
from rt_memory_patch import derive

ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser()
    for n in ('build','baseline','core','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();b=a.build;planpath=ROOT/'profiles/v106-rt-memory/plan.json';plan=json.loads(planpath.read_text());result=json.loads((b/'result.json').read_text())
    if a.output.exists():raise ValueError('New audit required')
    assert result['profile_sha256']==sha(planpath)
    assert sha(a.baseline/'kernel.macho')==plan['baseline_kernel_sha256']
    cap=json.loads((a.core/'capture.json').read_text());records=json.loads((a.core/'build/results.json').read_text());old=records[str(plan['core_job'])]
    original=Path(cap['files'][cap['jobs'][plan['core_job']]['file']]['path'])
    assert (b/'source/pmap.c').read_bytes()==derive(original.read_bytes())
    before=(a.baseline/'link.filelist').read_text().splitlines();after=(b/'link.filelist').read_text().splitlines()
    assert len(before)==len(after) and sum(x!=y for x,y in zip(before,after))==1
    assert [(x,y) for x,y in zip(before,after) if x!=y]==[(old['output'],str(b/'objects/pmap.o'))]
    for path,digest in {**result['unchanged_core_inputs'],**result['unchanged_archive_inputs']}.items():assert sha(Path(path))==digest
    assert sha(b/'objects/pmap.o')==result['pmap_object_sha256']
    vector=json.loads((b/'vector-audit.json').read_text());assert vector['existing_vectors_unchanged'] and vector['rt_helper_scalar']
    for name in ('native','kernel'):assert sha(b/(name+'.macho'))==result[name+'_sha256']
    native=(b/'native.macho').read_bytes();kernel=(b/'kernel.macho').read_bytes()
    address=symbol(native,'_wimg_to_pte')
    asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-d','--reloc',str(b/'objects/pmap.o')],text=True)
    hit=re.search(r'^([0-9a-f]+) <_wimg_to_pte>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',asm,re.M|re.S);assert hit
    instructions=re.findall(r'^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+',hit[2],re.M);assert instructions
    extent=int(instructions[-1],16)+4-int(hit[1],16)
    assert '_vm_first_phys' in hit[2] and '_vm_last_phys' in hit[2]
    assert at_va(native,address,extent)==at_va(kernel,address,extent)
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']
    verify_original_executables(b/'kernel.macho',json.loads((b/'kernel.json').read_text()),pre['original_executable_sha256'])
    def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e}
    assert exports(b/'native.macho')==exports(a.baseline/'native.macho')
    report={'accepted_static':True,'profile_sha256':sha(planpath),'kernel_sha256':sha(b/'kernel.macho'),'native_sha256':sha(b/'native.macho'),'changed_core_objects':1,'original_apple_executables_hashed':30,'managed_rt_helper_address':hex(address),'helper_bytes':extent,'helper_prelink_bytes_equal':True,'managed_range_symbols_bound':True,'existing_vector_instructions':vector['existing_vector_count'],'new_rt_helper_scalar':True,'unchanged_archives':len(result['unchanged_archive_inputs']),'hardware_tested':False}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
