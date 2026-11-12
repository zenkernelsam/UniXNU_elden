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
    profile=ROOT/'profiles/v100-hid-delivery/plan.json';plan=json.loads(profile.read_text())
    if report.get('profile')!='v100-hid-delivery' or report['profile_sha256']!=sha(profile) or report['baseline_kernel_sha256']!=plan['baseline_kernel_sha256']:
        raise ValueError('Wrong HID build profile')
    if sha(root/'kernel.macho')!=report['kernel_sha256'] or entry['source_sha256']!=report['kernel_sha256']:raise ValueError('Wrong pre-entry kernel')
    audit=json.loads((root/'kernel-audit.json').read_text());layout=json.loads((root/'entry-layout.json').read_text())
    if not audit['accepted_static'] or audit['kernel_sha256']!=report['kernel_sha256'] or audit['profile_sha256']!=sha(profile) or audit['original_apple_executables_hashed']!=30:
        raise ValueError('Missing independent kernel qualification')
    if layout['source_kernel_sha256']!=report['kernel_sha256'] or layout['baseline_kernel_sha256']!=plan['baseline_kernel_sha256'] or not layout['assembly_sections_equal'] or not layout['same_object_in_both_filelists']:
        raise ValueError('Missing entry layout proof')
    start,target,word=inspect_entry((root/'kernel.macho').read_bytes())
    contract=json.loads((ROOT/'profiles/v100-hid-delivery/entry-assembly.json').read_text())
    if target-start!=contract['target_delta'] or layout['entry_word']!=word or layout['return_target']!=hex(target) or entry['entry']['entry_target']!=target:
        raise ValueError('Entry target differs from the unchanged assembly')
    return {'profile':'v100-hid-delivery','profile_sha256':sha(profile),'pre_entry_kernel_sha256':report['kernel_sha256'],
            'entry_return_target':hex(target),'entry_assembly_sha256':layout['assembly_sha256'],'menu_unchanged_from':'v94-fbwc-observation','hardware_tested':False}
