"""Prove a relocated entry returns to the unchanged start.s implementation."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from darwinboot_preflight import parse_kernel
from make_entry_probe_kernel import branch_target
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def symbol(data,name):
    at=32;table=None
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        cmd,n=struct.unpack_from('<II',data,at)
        if cmd==2:table=struct.unpack_from('<4I',data,at+8)
        at+=n
    if table is None:raise ValueError('No symbols')
    off,count,strings,_=table;found=[]
    for i in range(count):
        key,kind,_,_,value=struct.unpack_from('<IBBHQ',data,off+16*i)
        if kind&0xe0 or kind&0x0e!=0x0e:continue
        start=strings+key
        if data[start:data.index(b'\0',start)].decode()==name:found.append(value)
    if len(found)!=1:raise ValueError('Ambiguous entry symbol: '+name)
    return found[0]
def entry(data):
    p=parse_kernel(data);word=struct.unpack_from('<I',data,p['entry_file_offset'])[0]
    if word&0xfc000000!=0x14000000:raise ValueError('Entry is not a direct B')
    target=branch_target(word,p['entry'])
    if p['entry']!=symbol(data,'__start') or target!=symbol(data,'_start_first_cpu'):raise ValueError('Entry does not target start_first_cpu')
    return p['entry'],target,word
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',choices=('v99-bt-runtime','v100-hid-delivery','v101-bt-clock','v102-bt-wake','v103-bt-version','v105-hid-display','v106-rt-memory'),default='v99-bt-runtime')
    for n in ('baseline','build','core','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();contract=json.loads((ROOT/'profiles'/a.profile/'entry-assembly.json').read_text())
    plan=json.loads((ROOT/'profiles'/a.profile/'plan.json').read_text())
    if sha(a.baseline/'kernel.macho')!=plan['baseline_kernel_sha256']:raise ValueError('Wrong entry baseline')
    cap=json.loads((a.core/'capture.json').read_text());results=json.loads((a.core/'build/results.json').read_text())
    index=contract['object_index'];job=cap['jobs'][index];r=results[str(index)];obj=Path(r['output'])
    if not job['file'].endswith(contract['source_suffix']) or not r['accepted'] or sha(obj)!=r['sha256'] or sections(obj)!=contract['expected_sections']:
        raise ValueError('Entry assembly differs from the frozen object contract')
    for folder in (a.baseline,a.build):
        if (folder/'link.filelist').read_text().splitlines().count(str(obj))!=1:raise ValueError('Build did not reuse the same entry object')
    symbols=global_symbols(obj);start=symbols['__start'];first=symbols['_start_first_cpu']
    if start[1]!=first[1] or first[2]-start[2]!=contract['target_delta']:raise ValueError('Entry object symbol layout changed')
    old=entry((a.baseline/'kernel.macho').read_bytes());new=entry((a.build/'kernel.macho').read_bytes())
    if old[1]!=0xfffffff00746a05c or old[2]!=new[2] or old[1]-old[0]!=new[1]-new[0] or new[1]-new[0]!=contract['target_delta']:
        raise ValueError('Entry branch semantics changed')
    result={'source_kernel_sha256':sha(a.build/'kernel.macho'),'baseline_kernel_sha256':sha(a.baseline/'kernel.macho'),
        'assembly_sha256':sha(obj),'assembly_sections_equal':True,'same_object_in_both_filelists':True,
        'entry_symbol':'__start','return_symbol':'_start_first_cpu','entry_word':new[2],'target_delta':new[1]-new[0],
        'entry_address':hex(new[0]),'return_target':hex(new[1]),'baseline_entry_address':hex(old[0]),'baseline_return_target':hex(old[1]),
        'hardware_tested':False}
    if a.output.exists():raise ValueError('New audit output required')
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
