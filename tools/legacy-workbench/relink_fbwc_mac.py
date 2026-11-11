"""Relink the frozen v91 with recovery-gated framebuffer mapping components."""
from pathlib import Path
import hashlib,io,json,os,shlex,struct,subprocess,sys,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r;p=r/'research/fbwc-20261111';out=r/'out/startup-basic-bundle-v92-20261108';out.mkdir(exist_ok=True);assert not (out/'artifacts.zip').exists()
oldout=r/'out/startup-basic-bundle-v91-20261108';old=json.loads((oldout/'result.json').read_text());changes=json.loads((p/'objects.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
for n,h in changes['source']['files'].items():assert sha((p/n).read_bytes())==h
assert changes['host_passed']
objroot=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
original_list=Path(old['link_command'][old['link_command'].index('-filelist')+1]);lines=original_list.read_text().splitlines();hits=[i for i,x in enumerate(lines) if x.endswith('/video_console.o')];assert len(hits)==1
lines[hits[0]]=str(p/'video_console.o');filelist=p/'link.filelist';filelist.write_text('\n'.join(lines)+'\n')
def link(dest,modified):
 cmd=list(old['link_command']);cmd[cmd.index('-o')+1]=str(dest)
 if modified:
  cmd[cmd.index('-filelist')+1]=str(filelist)
  for previous,new in [(old['objects']['ufs']['archive'],str(p/'libShengUfs.a')),(str(r/'research/preview-four-20261111/libNativeLegacyDisplay.late.a'),str(p/'libNativeLegacyDisplay.late.a'))]:
   assert sum(previous in x for x in cmd)==1,previous;cmd=[x.replace(previous,new) for x in cmd]
 with dest.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=objroot,env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option'),stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
def sections(f):
 b=f.read_bytes();at=32;result={}
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for i in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+80*i;n=b[x:x+16].split(b'\0')[0];seg=b[x+16:x+32].split(b'\0')[0];address,size,off=struct.unpack_from('<QQI',b,x+32);flags=struct.unpack_from('<I',b,x+64)[0]
    result[(seg,n)]=(address,size,flags,b'' if flags&255 in (1,12,18) else b[off:off+size])
  at+=length
 return result
control=p/'v91-control.macho';link(control,False);parent=r/'research/graphic-variant-cpu-20261111/kernel-build-v91/native-v91.macho'
assert sha(parent.read_bytes())==old['native_sha256'] and sections(control)==sections(parent)
native=p/'native-v92.macho';command=link(native,True)
line=next(s[2:] for s in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if s.startswith('+ ') and '/emit_prelink.py ' in s)
pre=shlex.split(line);pre[0]=sys.executable;pre[1]=str(r/'research/cpu-guard-20261111/tree/kits/XnuPrelink/emit_prelink.py');pre[pre.index('--kernel')+1]=str(native);pre[pre.index('--output')+1]=str(out/'kernel.macho')
if (out/'kernel.macho').exists():
 cached=json.loads((out/'result.json').read_text())
 assert sha(native.read_bytes())==cached['native_sha256'] and sha((out/'kernel.macho').read_bytes())==cached['kernel_sha256'],'Cannot reuse a different prelink'
else:
 with (p/'prelink.log').open('w') as log:subprocess.run(pre,cwd=r,stdout=log,stderr=subprocess.STDOUT,check=True)
source=dict(old['source']);source['files']=dict(source['files']);source['files']['SmpGuard.inc']=sha((p/'SmpGuard.inc').read_bytes());source['files']['EldenSdamRecovery.inc']=sha((p/'EldenSdamRecovery.inc').read_bytes())
objects=dict(old['objects']);objects['ufs']=dict(objects['ufs'],source=str(p/'ufs-src/NativeUfs.cpp'),source_sha256=sha((p/'ufs-src/NativeUfs.cpp').read_bytes()),archive=str(p/'libShengUfs.a'),archive_sha256=sha((p/'libShengUfs.a').read_bytes()),object_sha256=sha((p/'ShengUfs.o').read_bytes()),command=changes['objects']['ShengUfs.o']['command'])
build=dict(old,source=source,objects=objects,kernel_sha256=sha((out/'kernel.macho').read_bytes()),native_sha256=sha(native.read_bytes()),link_command=command,fbwc_delta=changes,fbwc_parent_native_sha256=old['native_sha256'],fbwc_control_link_reproduced=True)
(out/'result.json').write_text(json.dumps(build,indent=2)+'\n')
if (out/'derivative.json').exists():
 d=json.loads((out/'derivative.json').read_text());assert d['source_sha256']==build['kernel_sha256'] and d['derivative_sha256']==sha((out/'entry/probe/kernel.macho').read_bytes())
else:subprocess.run([sys.executable,r/'out/prepare_battery_telemetry_derivative.py','--attempt','3','--build-dir',out],check=True)
with zipfile.ZipFile(oldout/'artifacts.zip') as z:files={n:z.read(n) for n in z.namelist() if n!='artifact-hashes.json'}
for n in ('kernel.json','result.json','derivative.json'):files[n]=(out/n).read_bytes()
files['kernel.macho']=(out/'entry/probe/kernel.macho').read_bytes();files['cpu-irq-snapshot-result.json']=files['result.json']
with zipfile.ZipFile(io.BytesIO(files['compiled-source.zip'])) as z:sources={n:z.read(n) for n in z.namelist() if n!='manifest.json'};manifest=json.loads(z.read('manifest.json'))
for name,local in [('tree/scripts/driver-patches/EldenDisplayGlue.cpp','EldenDisplayGlue.cpp'),('tree/build/native-legacy-display/src/NativeLegacyDisplay.cpp','NativeLegacyDisplay.cpp'),('cpu/SmpGuard.inc','SmpGuard.inc'),('usb/EldenSdamRecovery.inc','EldenSdamRecovery.inc'),('fbwc/video_console.c','video_console.c'),('fbwc/WcPolicy.h','WcPolicy.h'),('fbwc/MappingCheck.c','MappingCheck.c'),('fbwc/TestWcPolicy.c','TestWcPolicy.c')]:sources[name]=(p/local).read_bytes()
for n in ('build_fbwc_objects_mac.py','relink_fbwc_mac.py'):sources['build-scripts/'+n]=(p/n).read_bytes()
manifest['files']={n:sha(b) for n,b in sources.items()};buf=io.BytesIO()
with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as z:
 for n,b in sources.items():z.writestr(n,b)
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
files['compiled-source.zip']=buf.getvalue()
report=json.loads(files['display-build-report.json']);report['archive_sha256']=changes['display_archive_sha256'];report['compiled_object_sha256']=sha((p/'NativeLegacyDisplay.o').read_bytes());report['late_object_sha256']=sha((p/'NativeLegacyDisplay.late.o').read_bytes())
report['source_sha256']['scripts/driver-patches/EldenDisplayGlue.cpp']=sha((p/'EldenDisplayGlue.cpp').read_bytes());report['source_sha256']['build/native-legacy-display/src/NativeLegacyDisplay.cpp']=sha((p/'NativeLegacyDisplay.cpp').read_bytes())
files['display-build-report.json']=(json.dumps(report,indent=2)+'\n').encode()
for n in ('libNativeLegacyDisplay.late.a','EldenDisplayGlue.o','NativeLegacyDisplay.o','NativeLegacyDisplay.late.o','MappingCheck.o'):files['display-archives/'+n]=(p/n).read_bytes()
for n in ('libShengUfs.a','ShengUfs.o'):files['cpu-archives/'+n]=(p/n).read_bytes()
fault=json.loads(files['fault-delta.json']);fault['archive_sha256']=changes['ufs_archive_sha256'];fault['object_sha256']=sha((p/'ShengUfs.o').read_bytes());fault['instructions']=changes['objects']['ShengUfs.o']['instructions'];fault['command']=changes['objects']['ShengUfs.o']['command'];fault['recompiled_with_fbwc_ready_hook']=True
files['fault-delta.json']=(json.dumps(fault,indent=2)+'\n').encode();files['fbwc-objects.json']=(p/'objects.json').read_bytes();files['fbwc-host.json']=(p/'host.json').read_bytes();files['fbwc-video_console.o']=(p/'video_console.o').read_bytes()
records={'files':{n:sha(b) for n,b in files.items()},'source_files':len(sources),'hardware_tested':False}
with zipfile.ZipFile(out/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('artifact-hashes.json',json.dumps(records,indent=2)+'\n')
print(json.dumps({'kernel_sha256':sha(files['kernel.macho']),'zip_sha256':sha((out/'artifacts.zip').read_bytes()),'source_files':len(sources),'device_tested':False},indent=2))
