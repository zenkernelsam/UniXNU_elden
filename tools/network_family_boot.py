"""Verify original network-family inclusion and native/entry continuity."""
from pathlib import Path
import hashlib,json
from audit_entry_relocation import entry as inspect_entry
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path):
    folder=Path(report_path).parent;r=json.loads(Path(report_path).read_text());e=json.loads(Path(entry_path).read_text());pp=ROOT/'profiles/v108-network-family/plan.json';p=json.loads(pp.read_text())
    assert r['profile']=='v108-network-family' and r['profile_sha256']==sha(pp)
    assert sha(folder/'native.macho')==p['baseline_native_sha256']==r['native_sha256']
    assert sha(folder/'kernel.macho')==r['kernel_sha256']==e['source_sha256']
    audit=json.loads((folder/'kernel-audit.json').read_text())
    assert audit['accepted_static'] and audit['kernel_sha256']==r['kernel_sha256'] and audit['profile_sha256']==sha(pp)
    assert audit['network_personalities']==p['personalities'] and audit['bundle_libraries']==p['bundle_libraries']
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];expected=dict(pre['original_executable_sha256']);expected[p['added_component']]=p['executable_sha256']
    k=json.loads((folder/'kernel.json').read_text());assert verify_original_executables(folder/'kernel.macho',k,expected)==31
    assert set(k['components'])==set(expected)
    layout=json.loads((folder/'entry-layout.json').read_text());assert layout['source_kernel_sha256']==r['kernel_sha256'] and layout['assembly_sections_equal'] and layout['same_object_in_both_filelists']
    start,target,word=inspect_entry((folder/'kernel.macho').read_bytes());contract=json.loads((pp.parent/'entry-assembly.json').read_text())
    assert target-start==contract['target_delta'] and layout['entry_word']==word and layout['return_target']==hex(target) and e['entry']['entry_target']==target
    assert r['network_personalities_preserved'] and r['original_existing30_preserved']
    return {'profile':'v108-network-family','profile_sha256':sha(pp),'pre_entry_kernel_sha256':r['kernel_sha256'],'entry_return_target':hex(target),'entry_assembly_sha256':layout['assembly_sha256'],'menu_unchanged_from':'v106-rt-memory','network_family_original':True,'original_apple_executables_hashed':31,'hardware_tested':False}
