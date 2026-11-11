"""Check the actual linked UCSI event objects and the exact reviewed sources."""
from pathlib import Path
import argparse,json,hashlib,re,subprocess
root=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==root
p=argparse.ArgumentParser();p.add_argument('--power-version',type=int,required=True);p.add_argument('--version',type=int,required=True);p.add_argument('--output',type=Path);p.add_argument('--cpu-guard',action='store_true');a=p.parse_args()
d=root/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient'
if a.cpu_guard:
    assert (a.version,a.power_version)==(81,35);d=root/'research/cpu-guard-20261111/archives/EldenPmicClient'
build=json.loads((d/'build.json').read_text());archive=Path(build['archive'])
src=root/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
out=a.output.resolve() if a.output else root/f'out/startup-basic-bundle-v{a.version}-20261108/ucsi-codegen'
assert out.resolve().is_relative_to(root/'out');out.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest();assert sha(archive.read_bytes())==build['archive_sha256']
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');objects={}
def run(args):return subprocess.run([str(x) for x in args],check=True,capture_output=True).stdout
for name in ['PmicClient.o','UcsiQuery.o']:
    data=(d/name).read_bytes();assert sha(data)==build['objects'][name]
    member=run([llvm/'llvm-ar','p',archive,name]);assert member==data or member==data+b'\n'*((-len(data))%8)
    text=run([llvm/'llvm-objdump','--disassemble','--reloc',d/name]).decode()
    ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),name
    (out/(name+'.txt')).write_text(text);objects[name]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
assert '0xda050000' in (src/'UcsiQuery.c').read_text()
assert 'elden_ucsi_enable_events(&ucsi,ucsiInit==2)' in (src/'PmicClient.cpp').read_text()
if a.power_version>=35:
    assert 'UINT64_C(0x00600000)' in (src/'UcsiQuery.c').read_text()
    assert 'q->notification_control=(uint32_t)command' in (src/'UcsiQuery.c').read_text()
    assert '_elden_ucsi_event_control' in (out/'UcsiQuery.o.txt').read_text()
report=dict(accepted=True,objects=objects,archive_sha256=build['archive_sha256'],hardware_tested=False,
    pdo_quirk_notifications=a.power_version>=35,
    sources={n:sha((src/n).read_bytes()) for n in ['UcsiQuery.c','UcsiQuery.h','PmicClient.cpp','TestUcsiEvents.c','TestUcsiQuery.c','TestPdQuery.c']})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
