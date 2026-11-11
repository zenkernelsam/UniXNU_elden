from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
from icon_cpu_trust import append
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[90,91],default=90);v=a.parse_args().version
r=Path(__file__).resolve().parents[1];port=Path('D:/Desktop/Git/UniXNU_elden');d=r/f'edl_work/icon-dependency-20261111/v{v}';old=d.parent
for n in ('iconservicesagent','signed.json','prepare.json'):shutil.copyfile(old/n,d/n)
sha=lambda b:hashlib.sha256(b).hexdigest()
library=(d/'EldenIconCpuMode.dylib').read_bytes();agent=(d/'iconservicesagent').read_bytes()
build=json.loads((d/'library-build.json').read_text());assert sha(library)==build['dylib_sha256']
assert sha((port/'package/EldenIconCpuMode.c').read_bytes())==build['source_sha256']
assert sha((port/'package/EldenIconCpuPaths.h').read_bytes())==build['paths_sha256']
for n,h in build['extra_sources'].items():assert sha((port/'package'/n).read_bytes())==h
tc,lh=append((r/'edl_work/icon-cpu-20261111/original.trustcache').read_bytes(),library);tc,ah=append(tc,agent);(d/'kernel.macho.trustcache').write_bytes(tc)
header='#pragma once\n'
for name,data in [('elden_icon_cpu_payload',library),('elden_icon_agent_payload',agent)]:
 header+='static const unsigned char '+name+'[] = {\n'+'\n'.join(','.join('0x%02x'%x for x in data[i:i+16])+',' for i in range(0,len(data),16))+'\n};\n'
files={n:(port/'patches/xnu'/n).read_bytes() for n in ('EldenIconCpuCommon.inc','EldenIconCpuPaths.h')};files['EldenIconCpuPayload.h']=header.encode()
base=r/'research/basic-bundle-20261108/source-compiled-v89'
manifest={'parent_exec_sha256':sha((base/'xnu/kern_exec.c').read_bytes()),'parent_common_sha256':sha((base/'xnu/EldenIconCpuCommon.inc').read_bytes()),'files':{n:sha(b) for n,b in files.items()},'library_sha256':sha(library),'agent_sha256':sha(agent),'library_cdhash':lh,'agent_cdhash':ah,'hardware_tested':False}
p=r/'research/graphic-variant-cpu-20261111'/('kernel-input' if v==90 else f'kernel-input-v{v}');p.mkdir(exist_ok=True)
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
(d/'kernel-integration.json').write_text(json.dumps(manifest,indent=2)+'\n');print({'library_bytes':len(library),'agent_unchanged':agent==(old/'iconservicesagent').read_bytes(),'trustcache_sha256':sha(tc)})
