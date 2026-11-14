"""Qualify source-built current output without private prior image hashes."""
from pathlib import Path
import hashlib,json
from audit_entry_relocation import entry as inspect_entry
from link_v91_kernel import native_identity,verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path,runtime):
    report_path=Path(report_path);entry_path=Path(entry_path);runtime=Path(runtime)
    r=json.loads(report_path.read_text());e=json.loads(entry_path.read_text());folder=report_path.parent
    if not r.get('native_contract_matches') or r.get('apple_execution_ranges')!=30 or r.get('core_objects')!=1010 or r.get('current_units')!=30 or r.get('copied_historical_objects')!=0:
        raise ValueError('Incomplete source-built current kernel')
    for n in ('native','kernel'):
        if sha(folder/(n+'.macho'))!=r[n+'_sha256']:raise ValueError('Build image changed')
    sec,cmd,sym=native_identity(folder/'native.macho');expected=json.loads((ROOT/'profiles/current-build/native-contract.json').read_text())
    if (sec,cmd,sym)!=(expected['sections'],expected['commands'],expected['symbols']):raise ValueError('Current native content differs')
    apple=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']['original_executable_sha256']
    verify_original_executables(folder/'kernel.macho',json.loads((folder/'kernel.json').read_text()),apple)
    if e['source_sha256']!=r['kernel_sha256']:raise ValueError('Entry input mismatch')
    start,target,word=inspect_entry((folder/'kernel.macho').read_bytes())
    if target-start!=7124 or e['entry']['entry_target']!=target:raise ValueError('Entry assembly contract differs')
    p=json.loads((ROOT/'profiles/v119-bt-exit/plan.json').read_text());rr=json.loads((ROOT/'profiles/v118-native17/runtime-report.json').read_text())
    for name,key in [('BlueTool','runtime_helper_sha256'),('runtime.trustcache','runtime_trust_sha256')]:
        if sha(runtime/name)!=p[key]:raise ValueError('Runtime mismatch')
    return {'profile':'current-source','profile_sha256':sha(ROOT/'profiles/current-build/plan.json'),
            'trust_sha256':p['runtime_trust_sha256'],'helper_sha256':p['runtime_helper_sha256'],
            'entry_return_target':hex(target),'entry_assembly_sha256':'818ed81e39f38ee6f13bdbf4f166e9e7a5d72f4b7a652d52872e2ba85ea53cc1',
            'mode':4,'hardware_tested':False,'text_fixups_hex':rr['text_fixups_after_hex']}

def qualify_gui(report_path,entry_path):
    report_path=Path(report_path);folder=report_path.parent;r=json.loads(report_path.read_text());e=json.loads(Path(entry_path).read_text())
    profile=ROOT/'profiles/gui-build';expected=json.loads((profile/'native-contract.json').read_text())
    if r.get('variant')!='gui' or not r.get('native_contract_matches') or r.get('apple_execution_ranges')!=31 or r.get('current_units')!=10 or r.get('core_objects')!=1010 or r.get('copied_historical_objects')!=0:
        raise ValueError('Incomplete source-built GUI kernel')
    if r['profile_sha256']!=sha(profile/'plan.json'):raise ValueError('GUI source manifest changed')
    for name in ('kernel','native'):
        if sha(folder/(name+'.macho'))!=r[name+'_sha256']:raise ValueError('GUI build changed')
    sec,cmd,sym=native_identity(folder/'native.macho')
    if (sec,cmd,sym)!=(expected['sections'],expected['commands'],expected['symbols']):raise ValueError('GUI native contract differs')
    apple=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']['original_executable_sha256'];network=json.loads((ROOT/'profiles/v108-network-family/plan.json').read_text());apple[network['added_component']]=network['executable_sha256']
    verify_original_executables(folder/'kernel.macho',json.loads((folder/'kernel.json').read_text()),apple)
    start,target,word=inspect_entry((folder/'kernel.macho').read_bytes())
    if target-start!=7124 or e['source_sha256']!=r['kernel_sha256'] or e['entry']['entry_target']!=target:raise ValueError('GUI entry differs')
    return {'profile':'v109-memory8','source_build':True,'profile_sha256':sha(profile/'plan.json'),'pre_entry_kernel_sha256':r['kernel_sha256'],
            'entry_return_target':hex(target),'bootstrap_storage_bytes':98304,'memory_window_bytes':0x200000000,'original_apple_executables_hashed':31,'hardware_tested':False}
