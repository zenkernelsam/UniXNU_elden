"""Qualify the exact-original Bluetooth socket/control syscall observation."""
from pathlib import Path
import hashlib,json
from link_v91_kernel import native_identity,verify_original_executables
from audit_entry_relocation import entry
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path,runtime):
    folder=Path(report_path).parent;r=json.loads(Path(report_path).read_text());e=json.loads(Path(entry_path).read_text());runtime=Path(runtime)
    if r.get('profile')!='bt-syscall-trace' or r['changed_core_objects']!=2 or r['key_state']!=2:raise ValueError('Wrong diagnostic build')
    for name,row in r['objects'].items():
        if sha(folder/name)!=row['source_sha256'] or not row['trace_helper_scalar'] or not row['original_vector_shapes_preserved']:raise ValueError('Source/vector observation differs')
    for path,h in r['source_inputs'].items():
        if sha(ROOT/path)!=h:raise ValueError('Diagnostic source/tool changed: '+path)
    for name in ('kernel','native'):
        if sha(folder/(name+'.macho'))!=r[name+'_sha256']:raise ValueError('Diagnostic artifact changed')
    contract=ROOT/'profiles/bt-syscall-trace/native-contract.json';expected=json.loads(contract.read_text());s,c,h=native_identity(folder/'native.macho')
    if (s,c,h)!=(expected['sections'],expected['commands'],expected['symbols']):raise ValueError('Diagnostic loaded content differs')
    originals=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']['original_executable_sha256']
    verify_original_executables(folder/'kernel.macho',json.loads((folder/'kernel.json').read_text()),originals)
    start,target,word=entry((folder/'kernel.macho').read_bytes())
    if target-start!=7124 or e['entry']['entry_target']!=target or e['source_sha256']!=r['kernel_sha256'] or r['entry_return_target']!=hex(target):raise ValueError('Entry contract differs')
    p=json.loads((ROOT/'profiles/v119-bt-exit/plan.json').read_text());table=json.loads((ROOT/'profiles/v118-native17/runtime-report.json').read_text())['text_fixups_after_hex']
    for name,key in [('BlueTool','runtime_helper_sha256'),('runtime.trustcache','runtime_trust_sha256')]:
        if sha(runtime/name)!=p[key]:raise ValueError('Runtime differs')
    return {'profile':'bt-syscall-trace','profile_sha256':sha(contract),'trust_sha256':p['runtime_trust_sha256'],'helper_sha256':p['runtime_helper_sha256'],
            'entry_return_target':hex(target),'entry_assembly_sha256':'818ed81e39f38ee6f13bdbf4f166e9e7a5d72f4b7a652d52872e2ba85ea53cc1','mode':4,'key_state':2,'text_fixups_hex':table,'hardware_tested':False}
