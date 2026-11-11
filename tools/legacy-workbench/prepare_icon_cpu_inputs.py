"""Prepare reviewable userspace inputs only; no boot or device deployment."""
from pathlib import Path
import hashlib,json,plistlib,shutil
r=Path(__file__).resolve().parents[1];ref=r/'research/iconservices-cpu-20261111';out=r/'edl_work/icon-cpu-20261111'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
build=json.loads((out/'build.json').read_text());assert build['mode']==0 and build['ad_hoc_signature_verified']
assert sha(out/'EldenIconCpuMode.dylib')==build['dylib_sha256']
src=Path('D:/Desktop/Git/UniXNU_elden/package/EldenIconCpuMode.c');assert sha(src)==build['source_sha256']
shutil.copyfile(src,ref/'EldenIconCpuMode.c')
binary=ref/'IconServices.text.bin';image=next(x for x in json.loads((ref/'manifest.json').read_text())['images'] if x['path'].endswith('/IconServices'))
assert sha(binary)==image['sha256'];b=binary.read_bytes();off=0x1ad28a294-image['base'];assert b[off:off+8]==bytes.fromhex('000840f9c0035fd6')
original=ref/'com.apple.iconservices.iconservicesagent.plist';assert sha(original)=='bd2ce94953d89cc578e71aba0933c9e2bc502095fbe1d00a2930bbebe11ec363'
before=plistlib.loads(original.read_bytes());after=dict(before)
assert before['EnvironmentVariables']=={} and before['UserName']=='_iconservices'
assert before['ProgramArguments']==['/System/Library/CoreServices/iconservicesagent']
after['EnvironmentVariables']={'ELDEN_ICON_CPU_MODE':'0','DYLD_INSERT_LIBRARIES':'/private/var/elden/EldenIconCpuMode.dylib'}
after['StandardErrorPath']='/dev/console'
dest=out/'com.apple.iconservices.iconservicesagent.plist';dest.write_bytes(plistlib.dumps(after,fmt=plistlib.FMT_BINARY))
assert {k:v for k,v in after.items() if k not in ('EnvironmentVariables','StandardErrorPath')}=={k:v for k,v in before.items() if k!='EnvironmentVariables'}
proof=dict(prepared=True,device_deployed=False,hardware_tested=False,boot_candidate_ready=False,
 source_sha256=sha(src),dylib_sha256=sha(out/'EldenIconCpuMode.dylib'),original_job_sha256=sha(original),candidate_job_sha256=sha(dest),
 unchanged_job_fields=[k for k in before if k!='EnvironmentVariables'],
 gate='Explicit process/UUID/getter-address/getter-bytes/ObjC ABI; no CoreImage global patch',
 pending=['Review RAM-only job redirection before launchd; original root remains read-only','Verify trust and dyld environment handling for the original _iconservices job','Capture installed marker and no recurrent GL fault, software Settings and real frame result','Physical finger confirmation remains separate'],
 limitations='Selects non-CoreImage compositor branches; effects may differ. Not a general software CoreImage implementation or a GPU driver.')
(out/'prepared-inputs.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
