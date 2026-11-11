"""Sign/verify the local metadata-only clone, never deploy or change original."""
from pathlib import Path
import hashlib,json,plistlib,shutil,subprocess
r=Path.cwd();assert r==Path('/Users/ciscohe/Desktop/UniXNU_elden');d=r/'out/icon-dependency-20261111';q=json.loads((d/'prepare.json').read_text())
src=d/'iconservicesagent.unsigned';assert hashlib.sha256(src.read_bytes()).hexdigest()==q['unsigned_sha256']
dst=d/'iconservicesagent';shutil.copyfile(src,dst)
subprocess.run(['codesign','--force','--sign','-','--identifier',q['identifier'],'--entitlements',str(d/'entitlements.plist'),str(dst)],check=True)
subprocess.run(['codesign','--verify','--strict',str(dst)],check=True)
ent=subprocess.run(['codesign','-d','--entitlements',':-',str(dst)],capture_output=True,check=True).stdout
assert plistlib.loads(ent)==q['entitlements']
b=dst.read_bytes()
assert b[q['uuid_offset']:q['uuid_offset']+16].hex()==q['new_uuid']
for name,v in q['code_sections'].items():assert hashlib.sha256(b[v['offset']:v['offset']+v['bytes']]).hexdigest()==v['sha256'],name
report=dict(signed_sha256=hashlib.sha256(b).hexdigest(),bytes=len(b),new_uuid=q['new_uuid'],original_text_sections_unchanged=True,entitlements_unchanged=True,ad_hoc_signature_verified=True,device_deployed=False,hardware_tested=False,exec_redirection_implemented=False)
(d/'signed.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
