"""Check actual ar members for exact declared original ADSP/QRTR type replacements.

Unchanged path is still checked against the link's original SHA fingerprint.
New path never accepts an undeclared archive/member, missing object or loose
filename-only match. Container metadata can vary; every real member is hashed.
"""
from pathlib import Path
import hashlib,subprocess
ALLOWED={'libEldenAdspRuntime.a':{'AdspProbe.o'},'libEldenPmicTransport.a':{'QcomGlink.o'}}
RUNTIME_MEMBERS={'libEldenSensorMemory.a':('changed_memory_binding_archive',{'MemoryBinding.o'}),
                 'libEldenMotionSsc.a':('changed_SSC_archive',{'NativeSsc.o'}),
                 'libShengUfs.a':('changed_runtime_UFS_archive',{'ShengUfs.o'})}
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
    name=before.name;allowed=ALLOWED.get(name);runtime=report.get('actual_sensor_runtime_composition') is True
    runtime_key=None
    if runtime and name in RUNTIME_MEMBERS:runtime_key,allowed=RUNTIME_MEMBERS[name]
    if allowed is None:raise ValueError('Existing archive lost or changed without a supported transport delta')
    # The QRTR profile declares its exact three actual type consumers. Older
    # profiles retain their original one-member gate; this is not a filename
    # allowlist for arbitrary edits or a global widening of archive checks.
    qrtr=report.get('single_original_ADSP_IPCRTR_QRTR_owner') is True
    if qrtr and name=='libEldenAdspRuntime.a':allowed={'AdspProbe.o','ShengQrtr.o','QcomQrtr.o'}
    declarations=report.get(runtime_key) if runtime_key else report.get('changed_transport_archives',{}).get(name)
    if declarations is None and qrtr:
        declarations=report.get('cumulative_transport_archives',{}).get(name)
    if not isinstance(declarations,dict):raise ValueError('Declared transport member proof absent')
    matches=[v for v in current_command if v.startswith('-Wl,-force_load,') and Path(v.split(',',2)[2]).name==name]
    if len(matches)!=1:raise ValueError('One actual replacement archive required')
    after=Path(matches[0].split(',',2)[2]);old=members(before);new=members(after)
    if qrtr:
        cumulative=report.get('cumulative_transport_archives',{}).get(name)
        # Pick the cumulative proof only when every actually read old/new
        # member matches that exact recorded chain. Current-parent declarations
        # remain the default; a mismatched chain never becomes an allow rule.
        if isinstance(cumulative,dict) and set(cumulative)==set(old)==set(new) and all(
            isinstance(cumulative[n],dict) and cumulative[n].get('before')==old[n] and cumulative[n].get('after')==new[n] for n in old):
            declarations=cumulative
    if old.keys()!=new.keys() or old.keys()!=declarations.keys():raise ValueError('Actual transport members differ from original/declaration')
    for n in old:
        row=declarations[n]
        if not isinstance(row,dict) or row.get('before')!=old[n] or row.get('after')!=new[n]:raise ValueError('Actual ar member hash differs from declaration: '+name+'/'+n)
    changed=[n for n in old if old[n]!=new[n]]
    if set(changed)!=allowed:raise ValueError('Only the exact original transport type consumers may change')
    return {'unchanged_path':False,'original_sha256':expected,'replacement_sha256':sha(after),'changed_members':changed,
            'all_actual_members_verified':True,'members':declarations}
