"""Bind v88's actual new archive and single-word helper to unchanged v87 inputs."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
r=Path(__file__).resolve().parents[1];old=r/'edl_work/basic-bundle-20261108/v87';new=r/'edl_work/basic-bundle-20261108/v88';proof=r/'research/preview-four-20261111'
osrc=r/'research/basic-bundle-20261108/source-compiled-v87';src=r/'research/basic-bundle-20261108/source-compiled-v88'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
om=json.loads((osrc/'manifest.json').read_text())['files'];nm=json.loads((src/'manifest.json').read_text())['files']
assert set(nm)-set(om)=={'build-scripts/relink_preview_four_mac.py','build-scripts/build_preview_four_mac.py'} and not set(om)-set(nm)
name='tree/scripts/driver-patches/EldenDisplayGlue.cpp'
assert {n for n in om if om[n]!=nm[n]}=={name}
assert (src/name).read_text()==(osrc/name).read_text().replace('__atomic_store_n(&previewAfterInput,2,','__atomic_store_n(&previewAfterInput,4,')
def members(path):
 b=path.read_bytes();assert b[:8]==b'!<arch>\n';at=8;out={}
 while at<len(b):
  h=b[at:at+60];assert len(h)==60 and h[58:60]==b'`\n'
  n=h[:16].decode().strip();size=int(h[48:58]);data=b[at+60:at+60+size];assert len(data)==size
  if n.startswith('#1/'):
   length=int(n[3:]);n=data[:length].rstrip(b'\0').decode();data=data[length:]
  else:n=n.rstrip('/')
  if not n.startswith('__.SYMDEF') and n:assert n not in out;out[n]=data
  at+=60+size+(size&1)
 assert at==len(b);return out
previous=proof/'parent-archive.a';archive=new/'final/display-archives/libNativeLegacyDisplay.late.a';obj=new/'final/display-archives/EldenDisplayGlue.o'
delta=json.loads((new/'final/display-delta.json').read_text());component=json.loads((proof/'audit.json').read_text())
assert component['accepted_object'] and delta['control_sections_equal']
assert sha(previous)==delta['parent_archive_sha256'] and sha(archive)==delta['archive_sha256']==component['archive_sha256']
a,b=members(previous),members(archive);assert a.keys()==b.keys()
assert [n for n in a if a[n]!=b[n]]==['EldenDisplayGlue.o']
assert b['EldenDisplayGlue.o'] in (obj.read_bytes(),obj.read_bytes()+b'\n'*((-obj.stat().st_size)%8))
assert sha(obj)==delta['object_sha256'] and sha(src/name)==delta['source_sha256']==component['source_sha256']
for folder in ('archives','cpu-archives'):
 for f in (old/'final'/folder).rglob('*'):
  if f.is_file():assert f.read_bytes()==(new/'final'/folder/f.relative_to(old/'final'/folder)).read_bytes()
for folder in ('pd-codegen','ucsi-codegen','touch-codegen'):
 (new/folder).mkdir(exist_ok=True)
 for f in (old/folder).iterdir():
  if f.is_file():shutil.copyfile(f,new/folder/f.name)
touch=json.loads((new/'touch-codegen/audit.json').read_text());touch['library_sha256']=sha(archive)
touch['unchanged_members_parent_archive_sha256']=sha(previous)
(new/'touch-codegen/audit.json').write_text(json.dumps(touch,indent=2)+'\n')
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
kernel=(new/'final/kernel.macho').read_bytes();parent=(old/'final/kernel.macho').read_bytes();s=m.symbols(kernel);ps=m.symbols(parent)
n='_elden_preview_after_input';assert s[n]==ps[n]
end=min(v for v in s.values() if v>s[n]);off=m.locate(kernel,s[n])[1];po=m.locate(parent,ps[n])[1]
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);ins=list(cs.disasm(kernel[off:off+end-s[n]],s[n]));pins=list(cs.disasm(parent[po:po+end-s[n]],s[n]))
assert len(ins)==len(pins)
changes=[(a,b) for a,b in zip(pins,ins) if a.bytes!=b.bytes];assert len(changes)==1
a,b=changes[0];assert a.mnemonic==b.mnemonic=='mov' and a.op_str.endswith(', #2') and b.op_str==a.op_str[:-1]+'4'
assert not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str) for i in ins)
preview=json.loads((old/'preview-codegen.json').read_text())
preview.update(source_sha256=sha(src/name),object_sha256=sha(obj),archive_sha256=sha(archive),parent_codegen_sha256=sha(old/'preview-codegen.json'),post_input_frames=4,per_boot_frames=10,linked_helper_address=hex(s[n]),linked_helper_delta=[a.op_str,b.op_str])
(new/'preview-codegen.json').write_text(json.dumps(preview,indent=2)+'\n')
for script in ('audit_cpu_asm_fixed.py','audit_icon_cpu_codegen.py','audit_ui_dispatch.py','prepare_icon_cpu_qualification.py'):
 q=subprocess.run([sys.executable,r/'edl_work'/script,'--version','88'],capture_output=True,text=True)
 (new/(script+'.log')).write_text(q.stdout+q.stderr);assert q.returncode==0,(script,q.stderr[-1800:]);print(script+' passed',flush=True)
out=new/'final/icon-cpu-focused-gestures-ipad-pro-cpu8';out.mkdir(exist_ok=True)
for f in (new/'final/icon-cpu-focused-gestures').glob('*-qualification.json'):shutil.copyfile(f,out/f.name)
(new/'reused-qualifications.json').write_text(json.dumps({'unchanged_archive_members':len(members(archive))-1,'only_changed_member':'EldenDisplayGlue.o','kernel_helper_delta':[a.op_str,b.op_str],'preview_source_sha256':sha(src/name),'power_cpu_archives_identical':True},indent=2)+'\n')
