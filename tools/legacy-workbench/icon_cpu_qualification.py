"""Verify the exact RAM-only IconServices integration and added trust entry."""
from pathlib import Path
import hashlib,json,re,sys
from icon_cpu_trust import append

def verify(root,version,d,kernel):
 if version>=65:
  from icon_dependency_qualification import verify as normal
  return normal(root,version,d,kernel)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 ref=root/'edl_work/icon-cpu-20261111';build=json.loads((ref/'build.json').read_text())
 src=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 assert sha(src/'package/EldenIconCpuMode.c')==build['source_sha256']
 lib=(ref/'EldenIconCpuMode.dylib').read_bytes();assert hashlib.sha256(lib).hexdigest()==build['dylib_sha256']
 assert kernel.count(lib)==1
 header=(src/'xnu/EldenIconCpuPayload.h').read_text()
 embedded=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',header));assert embedded==lib
 assert build['dylib_sha256'] in header
 original=(ref/'original.trustcache').read_bytes();tc,cdhash=append(original,lib)
 assert tc==(ref/'kernel.macho.trustcache').read_bytes()
 code=root/f'edl_work/basic-bundle-20261108/v{version}/icon-codegen.json';proof=json.loads(code.read_text())
 assert proof['accepted'] and proof['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 env=(src/'xnu/EldenIconCpuEnv.inc').read_text();stage=(src/'xnu/EldenIconCpuStage.inc').read_text()
 assert sha(src/'xnu/EldenIconCpuEnv.inc')==proof['env_sha256'] and sha(src/'xnu/EldenIconCpuStage.inc')==proof['stage_sha256']
 assert '"/System/Library/CoreServices/iconservicesagent"' in env and 'pointer_size != 8' in env
 if version>=64:
  assert 'EXECUTABLE_KEY "/System/Library/CoreServices/iconservicesagent"' in env
  executive=(src/'xnu/kern_exec.c').read_text()
  assert '#define EXECUTABLE_KEY "executable_path="' in executive
  assert 'imgp->ip_strings + strlen(EXECUTABLE_KEY)' in executive
  assert b'executable_path=/System/Library/CoreServices/iconservicesagent' in kernel
 assert 'O_CREAT|O_EXCL|O_NOFOLLOW' in stage and 'native_owner_data_seed_size!=UINT64_C(536870912)' in stage
 assert 'memcmp(check,elden_icon_cpu_payload,sizeof(elden_icon_cpu_payload))' in stage
 assert b'ELDEN_ICON_CPU environment appended' in kernel and b'ELDEN_ICON_CPU stage=' in kernel
 assert 'elden_icon_cpu_prepare(p, (user_addr_t)scratch_addr);' in (src/'xnu/kern_exec.c').read_text()
 return dict(library_sha256=build['dylib_sha256'],source_sha256=build['source_sha256'],cdhash=cdhash,
  trustcache_sha256=hashlib.sha256(tc).hexdigest(),codegen_sha256=sha(code),scope='only exact iconservicesagent; RAM Data; original launch job and root unchanged',hardware_tested=False)
