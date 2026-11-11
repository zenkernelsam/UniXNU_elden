"""Check actual display-preview code and linked archive membership after a build."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd().resolve()==r
p=argparse.ArgumentParser();p.add_argument('--version',type=int,required=True);p.add_argument('--cpu-guard',action='store_true');a=p.parse_args()
d=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/build/native-legacy-display'
if a.cpu_guard:
    assert a.version==81;d=r/'research/cpu-guard-20261111/tree/build/native-legacy-display'
obj=d/'EldenDisplayGlue.o';archive=d/'libNativeLegacyDisplay.late.a';llvm=Path('/Users/ciscohe/toolchains/llvm-20/bin')
data=obj.read_bytes();member=subprocess.run([llvm/'llvm-ar','p',archive,obj.name],capture_output=True,check=True).stdout
assert member==data or member==data+b'\n'*((-len(data))%8)
text=subprocess.run([llvm/'llvm-objdump','--disassemble','--reloc',obj],capture_output=True,check=True,text=True).stdout
ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M);assert ops
bad=[x for x in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x)];assert not bad,bad
src=(r/'scripts/driver-patches/EldenDisplayGlue.cpp').read_bytes()
assert b'statusBar[238*96]' in src and b'base+y*7616+(1666+x)*4' in src and b'getLength()!=23152640' in src
out=r/f'out/startup-basic-bundle-v{a.version}-20261108/preview-codegen';out.mkdir(exist_ok=True)
sha=lambda x:hashlib.sha256(x).hexdigest()
(out/'EldenDisplayGlue.txt').write_text(text)
report=dict(accepted=True,source_sha256=sha(src),object_sha256=sha(data),archive_sha256=sha(archive.read_bytes()),archive_member_equal=True,instructions=len(ops),fp_simd=False,crop=[1666,0,238,96],qualified_mapping_bytes=23152640,hardware_tested=False)
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
