"""Explicit v99/v101 runtime boot contracts; no device operations."""
from pathlib import Path
import copy,hashlib,json,tomllib
from fbwc_boot import template as framebuffer_template
from battery_boot import template as battery_template,arguments
from audit_entry_relocation import entry as inspect_entry
ROOT=Path(__file__).resolve().parents[1]
FIXUP=b'bluetoothd,ddd20,a0050034,1f2003d5\0'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def qualify(report_path,entry_path,runtime):
    if not report_path or not entry_path or not runtime:raise ValueError('Complete v99 build, entry and runtime inputs required')
    report=json.loads(Path(report_path).read_text());entry=json.loads(Path(entry_path).read_text());runtime=Path(runtime)
    name=report.get('profile')
    if name not in ('v99-bt-runtime','v101-bt-clock'):raise ValueError('Unsupported Bluetooth runtime profile')
    profile=ROOT/'profiles'/name/'plan.json';plan=json.loads(profile.read_text())
    if report['profile_sha256']!=sha(profile) or report.get('recompiled_core_objects')!=2 or report['original_apple_executables_hashed']!=30:
        raise ValueError('Unqualified v99 core build')
    if entry['source_sha256']!=report['kernel_sha256']:raise ValueError('Entry is not the v99 kernel')
    source=Path(report_path).parent/'kernel.macho';layout=json.loads((Path(report_path).parent/'entry-layout.json').read_text())
    if sha(source)!=report['kernel_sha256'] or layout['source_kernel_sha256']!=sha(source) or not layout['assembly_sections_equal'] or not layout['same_object_in_both_filelists']:
        raise ValueError('Missing qualified entry-layout proof')
    start,target,word=inspect_entry(source.read_bytes())
    contract=json.loads((profile.parent/'entry-assembly.json').read_text())
    if target-start!=contract['target_delta'] or layout['entry_word']!=word or layout['return_target']!=hex(target) or entry['entry']['entry_target']!=target:
        raise ValueError('Entry return does not match the unchanged startup assembly')
    if sha(runtime/'BlueTool')!=plan['runtime_helper_sha256'] or sha(runtime/'runtime.trustcache')!=plan['runtime_trust_sha256'] or sha(runtime/'EldenBtRuntimePayload.h')!=plan['runtime_header_sha256']:
        raise ValueError('Runtime inputs differ from compiled profile')
    return {'profile':name,'profile_sha256':sha(profile),'trust_sha256':plan['runtime_trust_sha256'],
            'helper_sha256':plan['runtime_helper_sha256'],'entry_return_target':hex(target),'entry_assembly_sha256':layout['assembly_sha256'],'mode':4,'hardware_tested':False}
def menu():
    before=battery_template(framebuffer_template(True));config=tomllib.loads(before)
    first=config['entry'][0];original=first['args'];tokens=original.split()
    # This trial uses real input and Bluetooth logs, not the one-shot software
    # Settings replay or its UIKit trace. Keep every physical input/charge guard.
    removed={'elden-ui-replay','elden-ui'}
    result=[t for t in tokens if t.partition('=')[0] not in removed]+['elden-wcn-power=4']
    expected=arguments(original)
    for key in removed:expected.pop(key)
    expected['elden-wcn-power']='4'
    text=before.replace('args = "'+original+'"','args = "'+' '.join(result)+'"',1)
    text=text.replace('title = "'+first['title']+'"','title = "Bluetooth runtime + battery - TEST"',1)
    new=tomllib.loads(text)
    if arguments(new['entry'][0]['args'])!=expected or len(new['entry'][0]['args'].replace('@SESSION@','0'*16).encode())>1023:
        raise ValueError('Bluetooth option contract or length differs')
    if new['entry'][1:]!=config['entry'][1:] or new['boot']!=config['boot']:raise ValueError('Fallback/menu behavior changed')
    return text
def adt(base):
    result=copy.deepcopy(base);found=[]
    def visit(n):
        for p in n['properties']:
            if p['name']=='text-fixups':found.append(p)
        for c in n['children']:visit(c)
    visit(result['tree'])
    if len(found)!=1:raise ValueError('Expected original fixup table')
    old=bytes.fromhex(found[0]['hex'])
    if not old.endswith(b'\0') or b'bluetoothd,' in old:raise ValueError('Prior Bluetooth fixup is not allowed')
    found[0]['hex']=(old+FIXUP).hex();result.pop('sha256',None)
    return result
