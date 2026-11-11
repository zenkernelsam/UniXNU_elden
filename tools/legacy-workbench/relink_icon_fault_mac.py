"""Link the bounded fault caller capture into the exact v88 driver set."""
from pathlib import Path
import hashlib,io,json,os,shlex,struct,subprocess,sys,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/icon-fault-trace-20261111';oldout=r/'out/startup-basic-bundle-v88-20261108'
out=r/'out/startup-basic-bundle-v89-20261108';out.mkdir(exist_ok=True);assert not (out/'artifacts.zip').exists()
old=json.loads((oldout/'result.json').read_text());fault=json.loads((p/'result.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
assert fault['host_passed'] and not fault['fp_simd'] and sha((p/'libShengUfs.a').read_bytes())==fault['archive_sha256']
for n,h in fault['source']['files'].items():assert sha((p/n).read_bytes())==h
objroot=r/'research/cpu-guard-20261111/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
def link(dest,changed):
 cmd=list(old['link_command']);cmd[cmd.index('-o')+1]=str(dest)
 if changed:
  original=old['objects']['ufs']['archive'];assert sum(original in x for x in cmd)==1
  cmd=[x.replace(original,str(p/'libShengUfs.a')) for x in cmd]
 with dest.with_suffix('.log').open('w') as log:subprocess.run(cmd,cwd=objroot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return cmd
def sections(path):
 b=path.read_bytes();at=32;result={}
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,length=struct.unpack_from('<II',b,at)
  if cmd==0x19:
   for j in range(struct.unpack_from('<I',b,at+64)[0]):
    x=at+72+80*j;n=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
    address,size,offset=struct.unpack_from('<QQI',b,x+32);kind=struct.unpack_from('<I',b,x+64)[0]
    result[seg+'/'+n]=(address,size,kind,b'' if kind&255 in (1,12,18) else b[offset:offset+size])
  at+=length
 return result
control=p/'v88-control.macho';link(control,False)
parent_native=r/'research/preview-four-20261111/native-v88.macho'
assert sha(parent_native.read_bytes())==old['native_sha256'] and sections(control)==sections(parent_native)
native=p/'native-v89.macho';command=link(native,True)
sys.path.insert(0,str(r/'scripts'));import make_entry_probe_kernel as maker
assert '_elden_icon_fault_capture' in maker.symbols(native.read_bytes())
line=next(x[2:] for x in (r/'out/ipados-console-20261107/build.log').read_text().splitlines() if x.startswith('+ ') and '/emit_prelink.py ' in x)
cmd=shlex.split(line);cmd[0]=sys.executable;cmd[1]=str(r/'research/cpu-guard-20261111/tree/kits/XnuPrelink/emit_prelink.py')
cmd[cmd.index('--kernel')+1]=str(native);cmd[cmd.index('--output')+1]=str(out/'kernel.macho')
with (p/'prelink.log').open('w') as log:subprocess.run(cmd,cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
objects=dict(old['objects']);objects['ufs']=dict(objects['ufs'],archive=str(p/'libShengUfs.a'),archive_sha256=fault['archive_sha256'],object_sha256=fault['object_sha256'],source=str(p/'src/NativeUfs.cpp'),source_sha256=sha((p/'src/NativeUfs.cpp').read_bytes()),command=fault['command'])
objects['icon_fault_capture']={'object_sha256':fault['capture_object_sha256'],'source_sha256':fault['source']['files']['IconFaultCapture.c'],'command':fault['capture_command']}
build=dict(old,kernel_sha256=sha((out/'kernel.macho').read_bytes()),native_sha256=sha(native.read_bytes()),link_command=command,objects=objects,fault_delta=fault,fault_control_reproduced=True,fault_parent_native_sha256=old['native_sha256'])
(out/'result.json').write_text(json.dumps(build,indent=2)+'\n')
subprocess.run([sys.executable,r/'out/prepare_battery_telemetry_derivative.py','--attempt','3','--build-dir',out],check=True)
with zipfile.ZipFile(oldout/'artifacts.zip') as z:files={n:z.read(n) for n in z.namelist() if n!='artifact-hashes.json'}
for n in ('kernel.json','result.json','derivative.json'):files[n]=(out/n).read_bytes()
files['kernel.macho']=(out/'entry/probe/kernel.macho').read_bytes();files['cpu-irq-snapshot-result.json']=files['result.json']
with zipfile.ZipFile(io.BytesIO(files['compiled-source.zip'])) as z:
 sources={n:z.read(n) for n in z.namelist() if n!='manifest.json'};manifest=json.loads(z.read('manifest.json'))
assert sha(sources['usb/EldenUsbDebugXnu.inc'])==fault['source']['parent_usb_sha256']
for n in fault['source']['files']:sources['usb/'+n]=(p/n).read_bytes()
for n in ('relink_icon_fault_mac.py','build_icon_fault_trace_mac.py'):sources['build-scripts/'+n]=(p/n).read_bytes()
manifest['files']={n:sha(b) for n,b in sources.items()}
buf=io.BytesIO()
with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as z:
 for n,b in sources.items():z.writestr(n,b)
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
files['compiled-source.zip']=buf.getvalue()
for n in ('libShengUfs.a','ShengUfs.o','IconFaultCapture.o'):files['cpu-archives/'+n]=(p/n).read_bytes()
files['fault-delta.json']=(p/'result.json').read_bytes();files['fault-host.json']=(p/'host.json').read_bytes()
records={'files':{n:sha(b) for n,b in files.items()},'source_files':len(sources),'hardware_tested':False}
with zipfile.ZipFile(out/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('artifact-hashes.json',json.dumps(records,indent=2)+'\n')
print(json.dumps({'kernel_sha256':sha(files['kernel.macho']),'zip_sha256':sha((out/'artifacts.zip').read_bytes()),'source_files':len(sources)},indent=2))
