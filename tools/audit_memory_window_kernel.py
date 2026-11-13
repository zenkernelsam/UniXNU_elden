"""Check linked pool size, exact entry changes and inherited Apple components."""
import argparse,hashlib,json,struct,sys,re,subprocess
from pathlib import Path
from audit_entry_relocation import symbol,entry
from audit_hid_kernel import at_va
from link_v91_kernel import verify_original_executables
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    if sys.flags.optimize:raise SystemExit('Validation requires assertions')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','build','components','core','llvm','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();b=a.build;old=a.baseline;c=a.components;pp=ROOT/'profiles/v109-memory8/plan.json';p=read(pp);r=read(b/'result.json');cr=read(c/'result.json')
    for n in ('native','kernel'):
        assert sha(old/(n+'.macho'))==p['baseline_'+n+'_sha256'] and sha(b/(n+'.macho'))==r[n+'_sha256']
    assert r['profile_sha256']==sha(pp) and r['component_report_sha256']==sha(c/'result.json')
    before=(old/'native.macho').read_bytes();after=(b/'native.macho').read_bytes();kernel=(b/'kernel.macho').read_bytes()
    rec=read(a.core/'build/results.json')['304'];oldobj=Path(rec['output']);newobj=c/'start.o'
    assert sha(oldobj)==rec['sha256'] and sha(newobj)==cr['objects']['start']['sha256']
    sec=sections(newobj);oldsec=sections(oldobj);assert set(sec)==set(oldsec)
    for n,v in sec.items():assert v[:2]==oldsec[n][:2]
    size=sec['__TEXT/__text'][0];s0=symbol(before,'__start')-global_symbols(oldobj)['__start'][2];s1=symbol(after,'__start')-global_symbols(newobj)['__start'][2]
    oldcode=at_va(before,s0,size);code=at_va(after,s1,size)
    changes=[{'offset':i,'before':hex(struct.unpack_from('<I',oldcode,i)[0]),'after':hex(struct.unpack_from('<I',code,i)[0])} for i in range(0,size,4) if oldcode[i:i+4]!=code[i:i+4]]
    rel=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-r',str(newobj)],text=True)
    relocs={int(m[1],16):m[2] for m in re.finditer(r'^([0-9a-f]+) ARM64_RELOC_PAGE21\s+(\S+)$',rel,re.M)}
    literals=[];relocated=[]
    def page_target(word,pc):
        assert word&0x9f000000==0x90000000
        imm=((word>>29)&3)|(((word>>5)&0x7ffff)<<2)
        if imm&(1<<20):imm-=1<<21
        return (pc&~4095)+(imm<<12)
    for change in changes:
        offset=change['offset'];v0=int(change['before'],16);v1=int(change['after'],16)
        if offset in relocs:
            name=relocs[offset];assert v0&31==v1&31
            assert page_target(v0,s0+offset)==symbol(before,name)&~4095
            assert page_target(v1,s1+offset)==symbol(after,name)&~4095
            relocated.append({**change,'symbol':name})
        else:literals.append(change)
    assert sorted((x['before'],x['after']) for x in literals)==sorted([('0xd2800202','0xd2800302'),('0x9140432a','0x9140632a'),('0x9140432a','0x9140632a')])
    assert at_va(kernel,s1,size)==code
    oldentry=entry(before);newentry=entry(after);assert oldentry[2]==newentry[2] and oldentry[1]-oldentry[0]==newentry[1]-newentry[0]==7124
    for image,wanted in ((before,65536),(after,98304)):
        begin=symbol(image,'_bootstrap_pagetables')
        # Symbol adjacency proves reserved storage, not merely a larger clear bound.
        table=b/'native.macho' if wanted==98304 else old/'native.macho'
        syms=global_symbols(table);current=syms['_bootstrap_pagetables'];nextoffset=min(v[2] for v in syms.values() if v[0]&0x0e==0x0e and v[1]==current[1] and v[2]>current[2]);nextva=begin+nextoffset-current[2]
        assert nextva-begin==wanted and begin%4096==0
        assert at_va(image,begin,wanted)==bytes(wanted)
    for name in ('pmap','arm_vm_init','start'):assert sha(c/(name+'.o'))==cr['objects'][name]['sha256']
    assert set((old/'link.filelist').read_text().splitlines())-set((b/'link.filelist').read_text().splitlines())==set(r['replacements'])
    for path,h in {**r['inherited_core_inputs'],**r['inherited_archives']}.items():assert sha(Path(path))==h
    pre=read(ROOT/'profiles/v91/image-build/plan.json')['prelink'];np=read(ROOT/'profiles/v108-network-family/plan.json');expected=dict(pre['original_executable_sha256']);expected[np['added_component']]=np['executable_sha256']
    assert verify_original_executables(b/'kernel.macho',read(b/'kernel.json'),expected)==31
    report={'accepted_static':True,'profile_sha256':sha(pp),'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'bootstrap_storage_bytes':98304,'start_bytes':size,'start_exact_changes':literals,'start_relocations':relocated,'entry_address':hex(newentry[0]),'entry_return_target':hex(newentry[1]),'entry_word':newentry[2],'target_delta':7124,'original_apple_executables_hashed':31,'all_inherited_inputs_verified':True,'hardware_tested':False}
    with a.output.open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
