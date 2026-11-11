"""Audit the exact optional charger-log objects; no device operations."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
root=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==root
p=argparse.ArgumentParser();p.add_argument('--power-version',type=int,required=True);p.add_argument('--version',type=int,required=True);a=p.parse_args()
builddir=root/f'out/battery-telemetry-v{a.power_version}-20261108/EldenPmicClient'
build=json.loads((builddir/'build.json').read_text());archive=Path(build['archive'])
source=root/f'research/battery-telemetry-20261108/source-input-v{a.power_version}'
out=root/f'out/startup-basic-bundle-v{a.version}-20261108/ulog-codegen'
out.mkdir(exist_ok=True);assert not (out/'audit.json').exists()
sha=lambda data:hashlib.sha256(data).hexdigest()
def run(cmd):return subprocess.run([str(x) for x in cmd],check=True,capture_output=True).stdout
llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
assert sha(archive.read_bytes())==build['archive_sha256']
objects={}
for name in ['UlogRead.o','PmicClient.o']:
    path=builddir/name;data=path.read_bytes();assert sha(data)==build['objects'][name]
    member=run([llvm/'llvm-ar','p',archive,name]);assert member==data or member==data+b'\n'*((-len(data))%8)
    text=run([llvm/'llvm-objdump','--disassemble','--reloc',path]).decode()
    ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
    assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops),name
    (out/(name+'.txt')).write_text(text)
    objects[name]=dict(sha256=sha(data),instructions=len(ops),fp_simd=False,archive_member_equal=True)
client=(source/'PmicClient.cpp').read_text()
assert 'static UlogIncoming ulogInbox;' in client and 'static EldenUlogRead ulog;' in client
assert '#define SHENG_ADSP_MAX_MESSAGE 560u' in (source/'ShengAdsp.h').read_text()
assert 'ulogEnable?ELDEN_ULOG_REPLY:2048,5' in client
assert ('ulogEnable=((ulogEnable==1||ulogEnable==2)&&votesEnable==1&&regsEnable==0)' if a.power_version>=27 else 'ulogEnable=(ulogEnable==1&&votesEnable==1&&regsEnable==0)') in client
assert not (source/'UlogRead.c').read_text().count('0x19')
report=dict(accepted=True,objects=objects,archive_sha256=build['archive_sha256'],native_tested=False,
            fixed_reply_bytes=8204,boot_request_cap=12 if a.power_version>=27 else 2,control_set=False,
            global_tx_message_bytes=560,large_storage='independent static RX slot and worker payload',
            sources={n:sha((source/n).read_bytes()) for n in ['PmicClient.cpp','UlogRead.c','UlogRead.h','TestUlogRead.c','PowerPathSchedule.h','AdspProbe.cpp','ShengAdsp.h']+(['TestUlogPaging.c'] if a.power_version>=27 else [])+(['DiagnosticGate.h','TestDiagnosticGate.c'] if a.power_version>=29 else [])})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
