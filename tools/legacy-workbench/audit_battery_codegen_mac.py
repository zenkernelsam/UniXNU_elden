"""Audit the actual battery publication archive members before first UI activation."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--power-version',type=int,required=True);a=p.parse_args()
d=r/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient';b=json.loads((d/'build.json').read_text())
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');archive=Path(b['archive']);sha=lambda data:hashlib.sha256(data).hexdigest()
assert sha(archive.read_bytes())==b['archive_sha256'];out=d/'battery-ui-codegen';out.mkdir(exist_ok=True);records={}
for name in ['BatterySnapshot.o','BatteryRuntime.late.o']:
    data=(d/name).read_bytes();assert sha(data)==b['objects'][name]
    member=subprocess.run([llvm/'llvm-ar','p',archive,name],capture_output=True,check=True).stdout
    assert member==data or member==data+b'\n'*((-len(data))%8)
    text=subprocess.run([llvm/'llvm-objdump','--disassemble','--reloc',d/name],capture_output=True,check=True,text=True).stdout
    ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),name
    (out/(name+'.txt')).write_text(text);records[name]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
report=dict(accepted=True,archive_sha256=sha(archive.read_bytes()),objects=records,hardware_ui_tested=False)
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
