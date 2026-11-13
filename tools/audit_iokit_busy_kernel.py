"""Audit the bounded observer's single-member replacement and inherited RT core."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from audit_entry_relocation import symbol
from audit_hid_kernel import at_va
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser()
    for n in ('build','baseline','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();b=a.build;profile=ROOT/'profiles/v107-iokit-busy/plan.json';plan=json.loads(profile.read_text());r=json.loads((b/'result.json').read_text())
    if a.output.exists():raise ValueError('New output required')
    assert r['profile']=='v107-iokit-busy' and r['profile_sha256']==sha(profile)
    for n in ('kernel','native'):
        assert sha(a.baseline/(n+'.macho'))==plan['baseline_'+n+'_sha256']
        assert sha(b/(n+'.macho'))==r[n+'_sha256']
    assert (b/'link.filelist').read_bytes()==(a.baseline/'link.filelist').read_bytes()
    assert sha(ROOT/'profiles/v107-iokit-busy/sources/EldenDisplayGlue.cpp')==r['source_sha256']==plan['glue_source_sha256']
    assert sha(b/'EldenDisplayGlue.o')==r['object_sha256']
    archive=b/'display/libNativeLegacyDisplay.late.a';changed=[]
    for name,item in r['members'].items():
        data=subprocess.check_output(['/usr/bin/ar','p',str(archive),name])
        assert hashlib.sha256(data).hexdigest()==item['archived_after']
        if item['before']!=item['archived_after']:changed.append(name)
    assert changed==['EldenDisplayGlue.o']
    for path,digest in {**r['inherited_core_inputs'],**r['inherited_archives']}.items():assert sha(Path(path))==digest
    native=(b/'native.macho').read_bytes();kernel=(b/'kernel.macho').read_bytes()
    symbols=subprocess.check_output([str(a.llvm/'llvm-nm'),'-n',str(b/'native.macho')],text=True)
    matches=[(int(m[1],16),m[2]) for m in re.finditer(r'^([0-9a-f]+) \S (__ZL15iokitBusyWorkerPvi)$',symbols,re.M)]
    assert len(matches)==1
    address,name=matches[0]
    next_address=min(int(m[1],16) for m in re.finditer(r'^([0-9a-f]+) \S ',symbols,re.M) if int(m[1],16)>address)
    assert 0<next_address-address<16384
    assert at_va(native,address,next_address-address)==at_va(kernel,address,next_address-address)
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];verify_original_executables(b/'kernel.macho',json.loads((b/'kernel.json').read_text()),pre['original_executable_sha256'])
    report={'accepted_static':True,'profile_sha256':sha(profile),'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'changed_archive_members':changed,'core_filelist_equal':True,'original_apple_executables_hashed':30,'worker_symbol':name,'worker_address':hex(address),'worker_bytes':next_address-address,'worker_prelink_equal':True,'source_sha256':r['source_sha256'],'hardware_tested':False}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
