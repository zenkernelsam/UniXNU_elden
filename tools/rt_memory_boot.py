"""Qualify the explicit v106 core-only successor before assembling its image."""
from pathlib import Path
import hashlib,json
from audit_entry_relocation import entry as inspect_entry
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path):
    folder=Path(report_path).parent;report=json.loads(Path(report_path).read_text());entry=json.loads(Path(entry_path).read_text());profile=ROOT/'profiles/v106-rt-memory/plan.json';plan=json.loads(profile.read_text())
    assert report['profile']=='v106-rt-memory' and report['profile_sha256']==sha(profile)
    assert sha(folder/'kernel.macho')==report['kernel_sha256']==entry['source_sha256']
    assert report['source_sha256']==plan['derived_source_sha256'] and report['recompiled_core_objects']==1
    audit=json.loads((folder/'kernel-audit.json').read_text());layout=json.loads((folder/'entry-layout.json').read_text())
    assert audit['accepted_static'] and audit['profile_sha256']==sha(profile) and audit['kernel_sha256']==sha(folder/'kernel.macho') and audit['original_apple_executables_hashed']==30
    assert layout['source_kernel_sha256']==report['kernel_sha256'] and layout['baseline_kernel_sha256']==plan['baseline_kernel_sha256'] and layout['assembly_sections_equal'] and layout['same_object_in_both_filelists']
    start,target,word=inspect_entry((folder/'kernel.macho').read_bytes());contract=json.loads((profile.parent/'entry-assembly.json').read_text())
    assert target-start==contract['target_delta'] and layout['entry_word']==word and layout['return_target']==hex(target) and entry['entry']['entry_target']==target
    return {'profile':'v106-rt-memory','profile_sha256':sha(profile),'pre_entry_kernel_sha256':report['kernel_sha256'],'entry_return_target':hex(target),'entry_assembly_sha256':layout['assembly_sha256'],'menu_unchanged_from':'v105-hid-display','hardware_tested':False}
