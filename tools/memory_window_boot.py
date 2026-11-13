"""Qualify the explicit v109 8GiB profile; old boot profiles stay frozen."""
from pathlib import Path
import copy,hashlib,json
from audit_entry_relocation import entry
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path):
    folder=Path(report_path).parent;r=json.loads(Path(report_path).read_text());e=json.loads(Path(entry_path).read_text());pp=ROOT/'profiles/v109-memory8/plan.json';p=json.loads(pp.read_text());audit=json.loads((folder/'kernel-audit.json').read_text())
    assert r['profile']=='v109-memory8' and r['profile_sha256']==sha(pp)==audit['profile_sha256']
    assert sha(folder/'kernel.macho')==r['kernel_sha256']==audit['kernel_sha256']==e['source_sha256']
    assert sha(folder/'native.macho')==r['native_sha256']==audit['native_sha256']
    assert audit['accepted_static'] and audit['bootstrap_storage_bytes']==24*4096 and len(audit['start_exact_changes'])==3
    start,target,word=entry((folder/'kernel.macho').read_bytes())
    assert target-start==7124 and hex(target)==audit['entry_return_target'] and word==audit['entry_word'] and e['entry']['entry_target']==target
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];np=json.loads((ROOT/'profiles/v108-network-family/plan.json').read_text());expected=dict(pre['original_executable_sha256']);expected[np['added_component']]=np['executable_sha256']
    assert verify_original_executables(folder/'kernel.macho',json.loads((folder/'kernel.json').read_text()),expected)==31
    assert r['memory_window_bytes']==p['intended_window_bytes']==0x200000000
    return {'profile':'v109-memory8','profile_sha256':sha(pp),'pre_entry_kernel_sha256':r['kernel_sha256'],'entry_return_target':hex(target),'bootstrap_storage_bytes':98304,'memory_window_bytes':0x200000000,'original_apple_executables_hashed':31,'hardware_tested':False}
def adt(config):
    out=copy.deepcopy(config);changed=0
    def visit(node):
        nonlocal changed
        for p in node['properties']:
            if p['name']=='darwinboot-memory-size':
                assert p['hex']=='0000008001000000';p['hex']='0000000002000000';changed+=1
        for c in node['children']:visit(c)
    visit(out['tree']);assert changed==1
    return out
