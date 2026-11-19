"""Check actual ar members for the two declared original ADSP type replacements.

Unchanged path is still checked against the link's original SHA fingerprint.
New path never accepts an undeclared archive/member, missing object or loose
filename-only match. Container metadata can vary; every real member is hashed.
"""
from pathlib import Path
import hashlib,subprocess
ALLOWED={'libEldenAdspRuntime.a':'AdspProbe.o','libEldenPmicTransport.a':'QcomGlink.o'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def members(p):
    names=subprocess.check_output(['ar','t',str(p)],text=True).splitlines()
    names=[n for n in names if not n.startswith('__.SYMDEF')]
    if not names or len(names)!=len(set(names)) or any(Path(n).name!=n for n in names):raise ValueError('Invalid actual archive member list')
    return {n:hashlib.sha256(subprocess.check_output(['ar','p',str(p),n])).hexdigest() for n in names}
def retained_archive(argument,current_command,report):
    if not argument.startswith('-Wl,-force_load,'):raise ValueError('Actual force-load archive argument required')
    before=Path(argument.split(',',2)[2]);expected=report.get('inherited_inputs',{}).get(str(before))
    if expected is None or sha(before)!=expected:raise ValueError('Original inherited archive fingerprint missing/changed')
    if current_command.count(argument)==1:return {'unchanged_path':True,'sha256':expected}
    name=before.name;member=ALLOWED.get(name)
    if member is None:raise ValueError('Existing archive lost or changed without a supported transport delta')
    declarations=report.get('changed_transport_archives',{}).get(name)
    if not isinstance(declarations,dict):raise ValueError('Declared transport member proof absent')
    matches=[v for v in current_command if v.startswith('-Wl,-force_load,') and Path(v.split(',',2)[2]).name==name]
    if len(matches)!=1:raise ValueError('One actual replacement archive required')
    after=Path(matches[0].split(',',2)[2]);old=members(before);new=members(after)
    if old.keys()!=new.keys() or old.keys()!=declarations.keys():raise ValueError('Actual transport members differ from original/declaration')
    for n in old:
        row=declarations[n]
        if not isinstance(row,dict) or row.get('before')!=old[n] or row.get('after')!=new[n]:raise ValueError('Actual ar member hash differs from declaration')
    changed=[n for n in old if old[n]!=new[n]]
    if changed!=[member]:raise ValueError('Only the exact original transport type consumer may change')
    return {'unchanged_path':False,'original_sha256':expected,'replacement_sha256':sha(after),'changed_members':changed,
            'all_actual_members_verified':True,'members':declarations}
