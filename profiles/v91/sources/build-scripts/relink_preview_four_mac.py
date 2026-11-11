"""Relink the frozen v87 with its single-instruction display observation delta."""
from pathlib import Path
import hashlib,io,json,os,shlex,struct,subprocess,sys,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/preview-four-20261111';base=r/'research/cpu-hold-20261111'
out=r/'out/startup-basic-bundle-v88-20261108';out.mkdir(exist_ok=True)
assert not (out/'artifacts.zip').exists(),'Do not overwrite a published export'
old=json.loads((base/'result.json').read_text());display=json.loads((p/'result.json').read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha((p/'libNativeLegacyDisplay.late.a').read_bytes())==display['archive_sha256']
assert sha((p/'EldenDisplayGlue.cpp').read_bytes())==display['source_sha256']
assert display['control_sections_equal']
objroot=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
def link(dest,changed):
 cmd=list(old['link_command']);cmd[cmd.index('-o')+1]=str(dest)
 if changed:
  source=str(r/'research/cpu-guard-20261111/tree/build/native-legacy-display/libNativeLegacyDisplay.late.a')
  assert sum(source in x for x in cmd)==1
  cmd=[x.replace(source,str(p/'libNativeLegacyDisplay.late.a')) for x in cmd]
 with dest.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=objroot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
def sections(path):
 b=path.read_bytes();n=struct.unpack_from('<I',b,16)[0];at=32;out={}
 for _ in range(n):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for j in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+j*80;sect=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
    address,size,offset=struct.unpack_from('<QQI',b,x+32);kind=struct.unpack_from('<I',b,x+64)[0]
    out[seg+'/'+sect]=(address,size,kind,b'' if kind&255 in (1,12,18) else b[offset:offset+size])
  at+=length
 return out
control=p/'v87-control.macho';link(control,False)
assert sections(control)==sections(base/'native.macho')
native=p/'native-v88.macho';cmd=link(native,True)
before=sections(control);after=sections(native);assert before.keys()==after.keys()
changes=[]
for name in before:
 a,b=before[name],after[name];assert a[:3]==b[:3],name
 if a[3]!=b[3]:
  assert name.endswith('/__text'),name
  changes.extend((name,a[0]+i) for i,(x,y) in enumerate(zip(a[3],b[3])) if x!=y)
assert len(changes)==1,changes
sys.path.insert(0,str(r/'scripts'));import make_entry_probe_kernel as maker
def runtime_symbols(path):
 b=path.read_bytes();at=32
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==2:
   off,count,strings,size=struct.unpack_from('<IIII',b,at+8);result={}
   for i in range(count):
    index,kind,sect,desc,value=struct.unpack_from('<IBBHQ',b,off+i*16)
    if kind&0xe0:continue # N_STAB: source paths and object timestamps, not runtime symbols.
    name=b[strings+index:b.index(b'\0',strings+index)].decode()
    result[name]=(kind,sect,desc,value)
   return result
  at+=length
 raise AssertionError('Missing symbol table')
assert runtime_symbols(native)==runtime_symbols(control)
line=next(s[2:] for s in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if s.startswith('+ ') and '/emit_prelink.py ' in s)
pre=shlex.split(line);pre[0]=sys.executable;pre[1]=str(r/'research/cpu-guard-20261111/tree/kits/XnuPrelink/emit_prelink.py')
pre[pre.index('--kernel')+1]=str(native);pre[pre.index('--output')+1]=str(out/'kernel.macho')
with (p/'prelink-v88.log').open('w') as log:subprocess.run(pre,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
build=dict(old,kernel_sha256=sha((out/'kernel.macho').read_bytes()),native_sha256=sha(native.read_bytes()),link_command=cmd,
 display_delta=display,display_parent_native_sha256=old['native_sha256'],display_single_byte_change=changes,display_control_reproduced=True)
(out/'result.json').write_text(json.dumps(build,indent=2)+'\n')
subprocess.run([sys.executable,r/'out/prepare_battery_telemetry_derivative.py','--attempt','3','--build-dir',out],check=True)
with zipfile.ZipFile(r/'out/startup-basic-bundle-v87-20261108/artifacts.zip') as z:
 files={n:z.read(n) for n in z.namelist() if n!='artifact-hashes.json'}
for n in ('kernel.json','result.json','derivative.json'):files[n]=(out/n).read_bytes()
files['kernel.macho']=(out/'entry/probe/kernel.macho').read_bytes()
files['cpu-irq-snapshot-result.json']=files['result.json']
report=json.loads(files['display-build-report.json']);report['archive_sha256']=display['archive_sha256']
report['source_sha256']['scripts/driver-patches/EldenDisplayGlue.cpp']=display['source_sha256']
files['display-build-report.json']=(json.dumps(report,indent=2)+'\n').encode()
with zipfile.ZipFile(io.BytesIO(files['compiled-source.zip'])) as z:
 sources={n:z.read(n) for n in z.namelist() if n!='manifest.json'};manifest=json.loads(z.read('manifest.json'))
sources['tree/scripts/driver-patches/EldenDisplayGlue.cpp']=(p/'EldenDisplayGlue.cpp').read_bytes()
for n in ('relink_preview_four_mac.py','build_preview_four_mac.py'):sources['build-scripts/'+n]=(p/n).read_bytes()
manifest['files']={n:sha(b) for n,b in sources.items()}
buffer=io.BytesIO()
with zipfile.ZipFile(buffer,'w',zipfile.ZIP_DEFLATED) as z:
 for n,b in sources.items():z.writestr(n,b)
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
files['compiled-source.zip']=buffer.getvalue()
for n in ('libNativeLegacyDisplay.late.a','EldenDisplayGlue.o'):files['display-archives/'+n]=(p/n).read_bytes()
files['display-delta.json']=(json.dumps(display,indent=2)+'\n').encode()
records={'files':{n:sha(b) for n,b in files.items()},'source_files':len(sources),'hardware_tested':False}
with zipfile.ZipFile(out/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('artifact-hashes.json',json.dumps(records,indent=2)+'\n')
print(json.dumps({'kernel_sha256':sha(files['kernel.macho']),'zip_sha256':sha((out/'artifacts.zip').read_bytes()),'native_byte_changes':changes,'source_files':len(sources)},indent=2))
