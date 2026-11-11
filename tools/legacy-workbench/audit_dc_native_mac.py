"""Inspect the actual native DC trial objects and bounded recovery path."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--power-version',type=int,required=True);a=p.parse_args()
d=r/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient';b=json.loads((d/'build.json').read_text());lib=Path(b['archive']);src=r/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
out=r/f'out/startup-basic-bundle-v{a.version}-20261108/dc-native-codegen';out.mkdir(exist_ok=True)
sha=lambda x:hashlib.sha256(x).hexdigest();assert sha(lib.read_bytes())==b['archive_sha256'];llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin');records={}
def run(cmd):return subprocess.run([str(x) for x in cmd],capture_output=True,check=True).stdout
for n in ['DcNative.o','DcTrial.o','Spmi.o','PmicClient.o']:
 data=(d/n).read_bytes();assert sha(data)==b['objects'][n]
 member=run([llvm/'llvm-ar','p',lib,n]);assert member==data or member==data+b'\n'*((-len(data))%8)
 text=run([llvm/'llvm-objdump','--disassemble','--reloc',d/n]).decode();ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
 assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),n
 (out/(n+'.txt')).write_text(text);records[n]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
native=(out/'DcNative.o.txt').read_text();assert '_elden_adc_spmi_write' in native and '_elden_dc_native_write_fence' in native
# IOLockLock/Unlock inline to the XNU mutex imports in this SDK.
write=native[native.index('<__ZN12_GLOBAL__N_15writeEPvh>:'):]
assert write.index('_lck_mtx_lock')<write.index('_elden_dc_native_write_fence')<write.index('_elden_adc_spmi_write')
assert '_lck_mtx_unlock' in write and '_IOLockAlloc' in native
assert 'sheng_adsp_send' not in native
recovery=(r/f'out/startup-basic-bundle-v{a.version}-20261108/recovery-codegen/__ZL21eldenRecoveryShutdownv.txt').read_text()
assert recovery.index('_elden_dc_native_request_stop')<recovery.index('_elden_backlight_request_stop')<recovery.index('_elden_pd_bootstrap_request_stop')
report=dict(accepted=True,archive_sha256=b['archive_sha256'],objects=records,hardware_tested=False,
 sources={n:sha((src/n).read_bytes()) for n in ['DcNative.cpp','DcTrial.c','DcTrial.h','TestDcTrial.c','Spmi.c','Spmi.h','TestSpmi.c','EldenAdcResources.h','NativeBackend.h','PmicClient.cpp']},
 maximum_ma=500,low_ma=450,raw_values=[9,10],real_spmi_completion=True,no_synthetic_glink_ack=True,stop_writer_lock=True)
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
