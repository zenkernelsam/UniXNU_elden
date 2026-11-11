"""Generate the optional exec/staging patch against the frozen current XNU file."""
from pathlib import Path
import difflib,hashlib,json
r=Path(__file__).resolve().parents[1];port=Path('D:/Desktop/Git/UniXNU_elden');d=r/'edl_work/icon-cpu-20261111'
report=json.loads((d/'build.json').read_text());lib=(d/'EldenIconCpuMode.dylib').read_bytes();sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(lib)==report['dylib_sha256'] and report['mode']==0 and len(lib)<128*1024
assert sha((port/'package/EldenIconCpuMode.c').read_bytes())==report['source_sha256']
header='#pragma once\n#define ELDEN_ICON_CPU_PAYLOAD_SHA "'+sha(lib)+'"\nstatic const unsigned char elden_icon_cpu_payload[] = {\n'
header+='\n'.join(','.join('0x%02x'%x for x in lib[i:i+16])+',' for i in range(0,len(lib),16))+'\n};\n'
(port/'patches/xnu/EldenIconCpuPayload.h').write_text(header,newline='\n')
before=(r/'research/iconservices-cpu-20261111/xnu-before/kern_exec.c').read_text();after=before
old='__attribute__((noinline))\nstatic int\nexec_extract_strings(struct image_params *imgp)'
assert after.count(old)==1
after=after.replace(old,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n#include "EldenIconCpuEnv.inc"\n#endif\n\n'+old)
old='\t/* Save space for envv[] NULL terminator */';assert after.count(old)==1
after=after.replace(old,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\terror = elden_icon_cpu_add_environment(imgp, new_ptr_size);\n\tif (error) goto bad;\n#endif\n\n'+old)
old='#include "EldenWatchdogPolicy.inc"';assert after.count(old)==1
after=after.replace(old,old+'\n#include "EldenIconCpuStage.inc"')
old='\tif (elden_policy_error) panic("ELDEN watchdog launch policy failed: %d", elden_policy_error);';assert after.count(old)==1
after=after.replace(old,old+'\n\telden_icon_cpu_prepare(p, (user_addr_t)scratch_addr);')
patch=''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/bsd/kern/kern_exec.c',tofile='b/bsd/kern/kern_exec.c'))
(port/'patches/xnu/elden-icon-cpu.patch').write_text(patch,newline='\n')
out=r/'research/iconservices-cpu-20261111/kernel-integration.json';out.write_text(json.dumps(dict(source_before_sha256=sha(before.encode()),source_after_sha256=sha(after.encode()),patch_sha256=sha(patch.encode()),library_sha256=sha(lib),header_sha256=sha(header.encode()),persistent_root_changed=False,hardware_tested=False),indent=2)+'\n')
print(out.read_text())
