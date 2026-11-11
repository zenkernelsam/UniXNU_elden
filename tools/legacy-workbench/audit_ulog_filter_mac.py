"""Inspect linked log-filter/reader objects and bounded shutdown integration."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--power-version',type=int,required=True);a=p.parse_args()
d=r/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient';b=json.loads((d/'build.json').read_text());archive=Path(b['archive'])
s=r/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
out=r/f'out/startup-basic-bundle-v{a.version}-20261108/ulog-filter-codegen';out.mkdir(exist_ok=True)
sha=lambda x:hashlib.sha256(x).hexdigest();assert sha(archive.read_bytes())==b['archive_sha256'];llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');objects={}
def run(cmd):return subprocess.run([str(x) for x in cmd],capture_output=True,check=True).stdout
for n in ['UlogFilter.o','UlogRead.o','PmicClient.o']:
 data=(d/n).read_bytes();assert sha(data)==b['objects'][n]
 member=run([llvm/'llvm-ar','p',archive,n]);assert member==data or member==data+b'\n'*((-len(data))%8)
 text=run([llvm/'llvm-objdump','--disassemble','--reloc',d/n]).decode();ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),n
 (out/(n+'.txt')).write_text(text);objects[n]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
source=(s/'UlogFilter.c').read_text();assert 'put(out+16,0x02000020);put(out+20,4);put(out+24,3)' in source and 'else put(out+24,4)' in source
recovery=r/f'out/startup-basic-bundle-v{a.version}-20261108/recovery-codegen/__ZL21eldenRecoveryShutdownv.txt';text=recovery.read_text()
assert text.index('_elden_ulog_filter_request_stop')<text.index('_elden_pd_bootstrap_request_stop')
report=dict(accepted=True,archive_sha256=b['archive_sha256'],objects=objects,diagnostic_mask='0x402000020',level=3,restore_mask=0,restore_level=4,shutdown_hook=True,hardware_tested=False,
 sources={n:sha((s/n).read_bytes()) for n in ['UlogFilter.c','UlogFilter.h','UlogRead.c','UlogRead.h','PmicClient.cpp','TestUlogFilter.c','TestUlogRead.c','TestUlogPaging.c']})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
