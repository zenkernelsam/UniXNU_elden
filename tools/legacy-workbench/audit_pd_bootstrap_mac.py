"""Verify actual new PD/thermal objects used by the selected power archive."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--power-version',type=int,required=True);p.add_argument('--version',type=int,required=True);p.add_argument('--cpu-guard',action='store_true');a=p.parse_args()
source=r/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
d=r/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient'
if a.cpu_guard:
    assert (a.version,a.power_version)==(81,35);d=r/'research/cpu-guard-20261111/archives/EldenPmicClient'
build=json.loads((d/'build.json').read_text())
out=r/f'out/startup-basic-bundle-v{a.version}-20261108/pd-codegen';out.mkdir(exist_ok=True);assert not (out/'audit.json').exists()
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');sha=lambda b:hashlib.sha256(b).hexdigest()
def run(args):return subprocess.run([str(x) for x in args],capture_output=True,check=True).stdout
archive=Path(build['archive']);assert sha(archive.read_bytes())==build['archive_sha256']
records={}
for name in ['PmicClient.o','ProtectionRuntime.o','PdBootstrap.o','UsbIcl.o']+(['PowerPathRead.o'] if a.power_version>=24 else []):
    data=(d/name).read_bytes();assert sha(data)==build['objects'][name]
    member=run([llvm/'llvm-ar','p',archive,name]);assert member==data or member==data+b'\n'*((-len(data))%8)
    text=run([llvm/'llvm-objdump','--disassemble','--reloc',d/name]).decode()
    ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),name
    (out/(name+'.txt')).write_text(text);records[name]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
report=dict(accepted=True,objects=records,archive_sha256=build['archive_sha256'],hardware_tested=False,
    sources={n:sha((source/n).read_bytes()) for n in ['PdBootstrap.c','PdBootstrap.h','PmicClient.cpp','UsbIcl.c','UsbIcl.h','ProtectionRuntime.cpp','PulseControl.h']+(['DiagnosticGate.h','TestDiagnosticGate.c'] if a.power_version>=29 else [])})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
