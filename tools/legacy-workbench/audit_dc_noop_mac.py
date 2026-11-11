"""Audit native fixed-value DC write, thermal token and shutdown integration."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--power-version',type=int,required=True);a=p.parse_args()
d=r/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient'
s=r/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
out=r/f'out/startup-basic-bundle-v{a.version}-20261108/dc-noop-codegen';out.mkdir(exist_ok=True)
b=json.loads((d/'build.json').read_text());archive=Path(b['archive'])
sha=lambda x:hashlib.sha256(x).hexdigest()
assert sha(archive.read_bytes())==b['archive_sha256']
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
def run(args):return subprocess.run([str(x) for x in args],check=True,capture_output=True).stdout
objects={};assembly={}
for n in ['DcNoop.o','PmicClient.o','ProtectionRuntime.o']+(['DcSpmiSurvey.o'] if a.power_version>=36 else []):
 data=(d/n).read_bytes();assert sha(data)==b['objects'][n]
 member=run([llvm/'llvm-ar','p',archive,n]);assert member==data or member==data+b'\n'*((-len(data))%8)
 text=run([llvm/'llvm-objdump','--disassemble','--reloc',d/n]).decode()
 ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),n
 (out/(n+'.txt')).write_text(text);assembly[n]=text
 objects[n]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
assert '_elden_thermal_cold_now' in assembly['PmicClient.o']
assert '_elden_dc_noop_next' in assembly['PmicClient.o'] and '_elden_dc_noop_request_stop' in assembly['PmicClient.o']
assert '_elden_thermal_cold_now' in assembly['ProtectionRuntime.o']
core=(s/'DcNoop.c').read_text()
assert 'put(out+20,0x2a52);put(out+24,10);' in core
assert core.count('put(out,32783)')==1 and 's->write_attempted=true' in core
assert 'v[0]!=0xdb||v[18]!=0xdb||v[1]!=1||v[2]!=0||v[3]!=10' in core
assert 'EldenDcNoopVerify' in core and 'bytes!=16||get(p+4)!=1||get(p+12)' in core
recovery=r/f'out/startup-basic-bundle-v{a.version}-20261108/recovery-codegen/__ZL21eldenRecoveryShutdownv.txt'
code=recovery.read_text();assert code.index('_elden_dc_noop_request_stop')<code.index('_elden_backlight_request_stop')<code.index('_elden_pd_bootstrap_request_stop')
sources={n:sha((s/n).read_bytes()) for n in ['DcNoop.c','DcNoop.h','TestDcNoop.c','PmicClient.cpp','ColdToken.h','ProtectionRuntime.cpp','PulseControl.h','PowerPathRead.c','PowerPathRead.h']}
if a.power_version>=36:
    survey=(s/'DcSpmiSurvey.cpp').read_text()
    assert '.write(' not in survey and 'ELDEN_ADC_SPMI_WRITER' not in survey and 'ELDEN_ADC_SPMI_OBSERVER' not in survey
    assert '0x727,0x729,0x72a' in survey and 'i<ELDEN_ADC_SPMI_APIDS' in survey
    assert '_elden_adc_spmi_write' not in assembly['DcSpmiSurvey.o']
    sources.update({n:sha((s/n).read_bytes()) for n in ['DcSpmiSurvey.cpp','NativeBackend.h','EldenAdcResources.h']})
report=dict(accepted=True,objects=objects,archive_sha256=b['archive_sha256'],sources=sources,
 fixed_bus=0,fixed_sid=7,fixed_register='0x2a52',only_write_raw=10,only_write_ma=500,
 max_write_attempts=1,higher_current_supported=False,shutdown_drain_before_pd=True,hardware_tested=False)
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
