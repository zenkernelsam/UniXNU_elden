"""Explicit v100 boot qualification, retaining every v94 menu/asset setting."""
import hashlib,json
from pathlib import Path
from audit_entry_relocation import entry as inspect_entry
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def qualify(report_path,entry_path):
    if not report_path or not entry_path:raise ValueError('HID build and entry reports required')
    report_path=Path(report_path);root=report_path.parent
    report=json.loads(report_path.read_text());entry=json.loads(Path(entry_path).read_text())
    name=report.get('profile')
    if report.get('native_contract_matches') and report.get('variant')=='gui':
        from current_boot import qualify_gui
        return qualify_gui(report_path,entry_path)
    if name=='v109-memory8':
        from memory_window_boot import qualify as qualify_memory
        return qualify_memory(report_path,entry_path)
    if name=='v106-rt-memory':
        from rt_memory_boot import qualify as qualify_rt
        return qualify_rt(report_path,entry_path)
    if name=='v108-network-family':
        from network_family_boot import qualify as qualify_network
        return qualify_network(report_path,entry_path)
    if name not in ('v100-hid-delivery','v105-hid-display','v107-iokit-busy'):raise ValueError('Unsupported HID profile')
    profile=ROOT/'profiles'/name/'plan.json';plan=json.loads(profile.read_text())
    if report['profile_sha256']!=sha(profile) or report['baseline_kernel_sha256']!=plan['baseline_kernel_sha256']:
        raise ValueError('Wrong HID build profile')
    if sha(root/'kernel.macho')!=report['kernel_sha256'] or entry['source_sha256']!=report['kernel_sha256']:raise ValueError('Wrong pre-entry kernel')
    audit=json.loads((root/'kernel-audit.json').read_text());layout=json.loads((root/'entry-layout.json').read_text())
    if not audit['accepted_static'] or audit['kernel_sha256']!=report['kernel_sha256'] or audit['profile_sha256']!=sha(profile) or audit['original_apple_executables_hashed']!=30:
        raise ValueError('Missing independent kernel qualification')
    if layout['source_kernel_sha256']!=report['kernel_sha256'] or layout['baseline_kernel_sha256']!=plan['baseline_kernel_sha256'] or not layout['assembly_sections_equal'] or not layout['same_object_in_both_filelists']:
        raise ValueError('Missing entry layout proof')
    start,target,word=inspect_entry((root/'kernel.macho').read_bytes())
    contract=json.loads((profile.parent/'entry-assembly.json').read_text())
    if target-start!=contract['target_delta'] or layout['entry_word']!=word or layout['return_target']!=hex(target) or entry['entry']['entry_target']!=target:
        raise ValueError('Entry target differs from the unchanged assembly')
    return {'profile':name,'profile_sha256':sha(profile),'pre_entry_kernel_sha256':report['kernel_sha256'],
            'entry_return_target':hex(target),'entry_assembly_sha256':layout['assembly_sha256'],
            'menu_unchanged_from':'v94-fbwc-observation' if name=='v100-hid-delivery' else None,
            'menu_policy':plan.get('menu_policy'),'hardware_tested':False}

def menu(qualified):
    from fbwc_boot import template
    if qualified['profile']=='v100-hid-delivery':return template(True)
    if qualified['profile'] not in ('v105-hid-display','v106-rt-memory','v107-iokit-busy','v108-network-family','v109-memory8'):raise ValueError('Unknown HID menu')
    import tomllib
    from battery_boot import template as battery_template,arguments
    base=battery_template(template(True))
    config=tomllib.loads(base);before=config['entry'][0]['args']
    expected=arguments(before)
    if expected.get('smp-hold')!='240':raise ValueError('Unexpected prior hold')
    after=before.replace('smp-hold=240','smp-hold=600');expected['smp-hold']='600'
    result=base.replace('args = "'+before+'"','args = "'+after+'"',1)
    result=result.replace('title = "'+config['entry'][0]['title']+'"','title = "iPadOS touch + display + battery - TEST"',1)
    changed=tomllib.loads(result)
    if arguments(changed['entry'][0]['args'])!=expected or changed['entry'][1:]!=config['entry'][1:] or changed['boot']!=config['boot']:
        raise ValueError('HID combined menu contract differs')
    if len(after.replace('@SESSION@','0'*16).encode())>1023:raise ValueError('Boot arguments overflow')
    return result
