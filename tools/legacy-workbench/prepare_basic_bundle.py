"""Prepare one exact-source battery/touch kernel, without device operations."""
import argparse,hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--version',type=int,default=2);p.add_argument('--power-version',type=int,default=4)
p.add_argument('--usb-max-ma',type=int,choices=(100,500),default=100)
p.add_argument('--backlight-version',type=int,default=0)
p.add_argument('--thermal-protection',action='store_true')
p.add_argument('--touch-manifest',type=Path,help='Reviewed current touch-source SHA manifest under research; preserves the original freeze')
p.add_argument('--icon-cpu',action='store_true',help='Compile the explicitly gated RAM-only IconServices integration')
p.add_argument('--icon-dependency',action='store_true',help='Use the normal dependency/verified RAM executable integration')
p.add_argument('--ui-dispatch',action='store_true',help='Add the reviewed one-shot UIKit event observation to v72 frozen diagnostic inputs')
p.add_argument('--recovery-source',type=Path,help='Explicit compatible frozen recovery source under research')
a=p.parse_args();assert 2<=a.version<=99
assert not a.icon_cpu or a.version>=62
assert not a.icon_dependency or (a.icon_cpu and a.version>=65)
assert not a.ui_dispatch or (a.icon_dependency and a.version>=74)
assert 4<=a.power_version<=99
assert 0<=a.backlight_version<=99
version='v'+str(a.version)
OUT=ROOT/('edl_work/basic-bundle-20261108/'+version);OUT.mkdir(parents=True,exist_ok=True)
source=(ROOT/'edl_work/battery-telemetry-20261108/attempt3/build_startup_battery_telemetry.py').read_text()
if a.version>=39:
    old="'EldenSpmiRestart.h','EldenSdamRecovery.inc'"
    assert source.count(old)==1
    source=source.replace(old,old+",'EldenRebootOwnership.h'")
assert source.count('out/startup-battery-telemetry-v3-20261108')==1
source=source.replace('out/startup-battery-telemetry-v3-20261108','out/startup-basic-bundle-'+version+'-20261108')
assert source.count('out/battery-telemetry-v3-20261108')==2
source=source.replace('out/battery-telemetry-v3-20261108','out/battery-telemetry-v'+str(a.power_version)+'-20261108')
marker="env['XNU_ELDEN_USB_DEBUG']='1'";assert source.count(marker)==1
source=source.replace(marker,marker+"\nenv['ELDEN_USB_CONFIG_MAX_MA']='"+str(a.usb_max_ma)+"'")
source=source.replace(marker,marker+"\nenv['XNU_ELDEN_TOUCH_RUNTIME']='1'")
if a.thermal_protection:
    source=source.replace(marker,marker+"\nenv['XNU_ELDEN_THERMAL_RUNTIME']='1'")
touch=(ROOT/'edl_work/build_startup_touch_runtime.py').read_text()
start=touch.index("for folder in ('drivers/EldenNvt36536'")
end=touch.index("log=out/'build.log'",start)
block=touch[start:end]
block=block.replace("XNU_ELDEN_MODEM_READER='0',XNU_ELDEN_POWER_RUNTIME='0'", "XNU_ELDEN_MODEM_READER='1',XNU_ELDEN_POWER_RUNTIME='1'")
assert "XNU_ELDEN_POWER_RUNTIME='1'" in block
assert source.count("log=out/'build.log'")==1
wifi='''for folder in ('drivers/EldenWifi',):
    for src in (root/folder).iterdir():
        if src.is_file():
            dest=tree/folder/src.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
for name in ('package/EldenWifiFirmwareInputs.h','soc/sm8750/include/EldenWifiResources.h'):
    dest=tree/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,dest)
env.update(ELDEN_WIFI_FW_CHECK_BUILD='1',ELDEN_WIFI_SOURCE=str(tree/'drivers/EldenWifi'),
    ELDEN_WIFI_RESOURCE_HEADER=str(tree/'soc/sm8750/include/EldenWifiResources.h'),
    ELDEN_WIFI_INPUT_HEADER=str(tree/'package/EldenWifiFirmwareInputs.h'))
'''
source=source.replace("log=out/'build.log'",block+'\n'+wifi+"\nlog=out/'build.log'",1)
if a.backlight_version:
    marker="env['XNU_EXTRA_LD_ARCHIVES']=' '.join('-Wl,-force_load,'+str(path) for path in paths)"
    assert source.count(marker)==1
    backlight='''
backlight=root/'out/backlight-runtime-vBACKLIGHT_VERSION-20261109'
backlight_report=json.loads((backlight/'build.json').read_text())
assert backlight_report['arm64_ios17'] and not backlight_report['automatic_initializers']
for name,digest in backlight_report['objects'].items():
    assert hashlib.sha256((backlight/name).read_bytes()).hexdigest()==digest
backlight_archive=Path(backlight_report['archive'])
assert hashlib.sha256(backlight_archive.read_bytes()).hexdigest()==backlight_report['archive_sha256']
env['XNU_EXTRA_LD_ARCHIVES']+=' -Wl,-force_load,'+str(backlight_archive)
env['XNU_ELDEN_BACKLIGHT_RUNTIME']='1'
'''.replace('BACKLIGHT_VERSION',str(a.backlight_version))
    source=source.replace(marker,marker+'\n'+backlight)
if a.icon_cpu:
    # OWNER_DATA_MOUNT is intentionally a kern_exec.o-only flag upstream.
    # Use a distinct macro for the two cross-file observers; retain all default
    # compiler options instead of replacing them with only this definition.
    marker="log=out/'build.log'";assert source.count(marker)==1
    source=source.replace(marker,"""flag_source=(tree/'scripts/build-xnu-4k-baseline.sh').read_text()
flag_defaults=[line[len('EXTRA_CFLAGS=${XNU_EXTRA_CFLAGS:-'):-1] for line in flag_source.splitlines() if line.startswith('EXTRA_CFLAGS=${XNU_EXTRA_CFLAGS:-') and line.endswith('}')]
assert len(flag_defaults)==1
observer_flags=env.get('XNU_EXTRA_CFLAGS',flag_defaults[0])
assert 'XNU_ELDEN_ICON_OBSERVER' not in observer_flags
env['XNU_EXTRA_CFLAGS']=observer_flags+' -DXNU_ELDEN_ICON_OBSERVER=1'
"""+marker,1)
    marker="last=root/'patches/xnu/elden-display-mode-observer.patch'";assert source.count(marker)==1
    peel="""icon_patch=root/'patches/xnu/elden-icon-cpu.patch'
icon_apply=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(icon_apply+['--reverse','--check',str(icon_patch)],capture_output=True).returncode==0:
    subprocess.run(icon_apply+['--reverse',str(icon_patch)],check=True)
"""
    source=source.replace(marker,peel+marker)
    old="'elden-display-mode-observer.patch'):";assert source.count(old)==1
    source=source.replace(old,"'elden-display-mode-observer.patch','elden-icon-cpu.patch'):")
    marker="shutil.copyfile(root/'patches/xnu/EldenWatchdogPolicy.inc',tree/'.cache/xnu-work/bsd/kern/EldenWatchdogPolicy.inc')"
    assert source.count(marker)==1
    source=source.replace(marker,marker+"\nfor name in ('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h'):\n    shutil.copyfile(root/'patches/xnu'/name,tree/'.cache/xnu-work/bsd/kern'/name)")
    if a.icon_dependency:
        source=source.replace("('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h')", "('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h','EldenIconCpuCommon.inc','EldenIconCpuPaths.h')")
if a.ui_dispatch:
    marker="icon_patch=root/'patches/xnu/elden-icon-cpu.patch'";assert source.count(marker)==1
    source=source.replace(marker,"""ui_patch=root/'patches/xnu/elden-ui-dispatch.patch'
ui_apply=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(ui_apply+['--reverse','--check',str(ui_patch)],capture_output=True).returncode==0:
    subprocess.run(ui_apply+['--reverse',str(ui_patch)],check=True)
"""+marker)
    marker="'elden-display-mode-observer.patch','elden-icon-cpu.patch'):";assert source.count(marker)==1
    source=source.replace(marker,"'elden-display-mode-observer.patch','elden-icon-cpu.patch','elden-ui-dispatch.patch'):")
    marker="log=out/'build.log'";assert source.count(marker)==1
    source=source.replace(marker,"""env['XNU_EXTRA_CFLAGS']+=' -DXNU_ELDEN_UI_DISPATCH=1'
for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h'):
    shutil.copyfile(root/'patches/xnu'/name,tree/'.cache/xnu-work/bsd/kern'/name)
"""+marker)
script=OUT/'build_basic_bundle.py';script.write_text(source,encoding='utf-8',newline='\n')
snapshot=ROOT/'research/nt36536-runtime-20261108/source-compiled'
old=json.loads((snapshot/'source-manifest.json').read_text())
touch_files={n:h for n,h in old['files'].items() if n.startswith(('drivers/EldenNvt36536/','package/EldenTouchBoard.h','soc/sm8750/include/EldenTouchResources.h'))}
if a.touch_manifest:
    manifest_path=a.touch_manifest.resolve()
    assert manifest_path.is_relative_to((ROOT/'research').resolve())
    reviewed=json.loads(manifest_path.read_text())
    assert reviewed['baseline_manifest_sha256']==hashlib.sha256((snapshot/'source-manifest.json').read_bytes()).hexdigest()
    assert reviewed['hardware_tested'] is False and reviewed['scope'] in ('touch-engine-dma-survey','touch-engine-dma-identify','touch-engine-dma-input')
    assert set(touch_files)<=set(reviewed['files'])
    for name in reviewed['files']:
        assert name in touch_files or (name.startswith('drivers/EldenNvt36536/') and '/' not in name[len('drivers/EldenNvt36536/'):])
    touch_files=reviewed['files']
inputs={};proof={}
for name,digest in touch_files.items():
    path=(DEVICE/name).resolve();assert path.is_relative_to(DEVICE.resolve())
    data=path.read_bytes();assert hashlib.sha256(data).hexdigest()==digest,name
    proof[name]=digest;inputs[name]=data
for path in ['scripts/build-elden-gop.py','scripts/build-elden-hid.py','scripts/build-sheng-ufs-probe.py','scripts/driver-patches/EldenUsbDebugXnu.inc','scripts/driver-patches/EldenHidGlue.cpp','scripts/driver-patches/EldenDisplayGlue.cpp','scripts/driver-patches/EldenUserWaitProbe.c',
             'research/nt36536-runtime-20261108/hid-owned-slots.json']:
    p=DEVICE/path if not path.startswith('research/') else ROOT/path
    inputs[path]=p.read_bytes()
if a.backlight_version:
    name='scripts/driver-patches/EldenPowerStartup.inc';inputs[name]=(DEVICE/name).read_bytes()
if a.thermal_protection:
    name='scripts/driver-patches/EldenSdamRecovery.inc';inputs[name]=(DEVICE/name).read_bytes()
    if a.recovery_source:
        recovery=a.recovery_source.resolve();assert recovery.is_relative_to((ROOT/'research').resolve())
        assert recovery.name=='EldenSdamRecovery.inc';inputs[name]=recovery.read_bytes()
    if a.version>=39:
        name='scripts/driver-patches/EldenRebootOwnership.h';inputs[name]=(DEVICE/name).read_bytes()
for path in ['GeniSpi.c','GeniSpi.h']:
    inputs['research/qcom-geni-spi-20261107/'+path]=(ROOT/'research/qcom-geni-spi-20261107'/path).read_bytes()
assert len(proof)>=9,'Complete touch code/resources must match the prior successful build'
for p in (DEVICE/'drivers/EldenWifi').iterdir():
    if p.is_file():inputs['drivers/EldenWifi/'+p.name]=p.read_bytes()
for name in ('package/EldenWifiFirmwareInputs.h','soc/sm8750/include/EldenWifiResources.h'):
    inputs[name]=(DEVICE/name).read_bytes()
inputs['out/build_basic_bundle-'+version+'.py']=script.read_bytes()
if a.icon_cpu:
    for n in ('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h','elden-icon-cpu.patch'):
        inputs['patches/xnu/'+n]=(DEVICE/'patches/xnu'/n).read_bytes()
    inputs['package/EldenIconCpuMode.c']=(DEVICE/'package/EldenIconCpuMode.c').read_bytes()
    if a.icon_dependency:
        for n in ('EldenIconCpuCommon.inc','EldenIconCpuPaths.h'):
            inputs['patches/xnu/'+n]=(DEVICE/'patches/xnu'/n).read_bytes()
        inputs['package/EldenIconCpuPaths.h']=(DEVICE/'package/EldenIconCpuPaths.h').read_bytes()
if a.ui_dispatch:
    audit=ROOT/'research/ui-dispatch-20261111'
    delta=json.loads((audit/'source-delta.json').read_text())
    for name,digest in delta['candidate_files'].items():
        raw=(audit/'candidate-source'/name).read_bytes()
        assert hashlib.sha256(raw).hexdigest()==digest
        key=name.replace('\\','/');assert key in inputs
        inputs[key]=raw
    for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h','elden-ui-dispatch.patch'):
        inputs['patches/xnu/'+name]=(DEVICE/'patches/xnu'/name).read_bytes()
manifest={n:hashlib.sha256(b).hexdigest() for n,b in inputs.items()}
archive=OUT/'source-upload.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in inputs.items():z.writestr(n,b)
    z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
install='''from pathlib import Path
import hashlib,json,shutil,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden').resolve()
p=r/'out/basic-bundle-source-upload-%s.zip'
assert hashlib.sha256(p.read_bytes()).hexdigest()==%r
with zipfile.ZipFile(p) as z:
 m=json.loads(z.read('manifest.json'))
 assert set(z.namelist())==set(m)|{'manifest.json'} and len(z.namelist())==len(set(z.namelist()))
 for n,h in m.items():
  t=(r/n).resolve();assert t.is_relative_to(r) and not t.is_symlink()
  b=z.read(n);assert hashlib.sha256(b).hexdigest()==h
  if t.exists() and t.read_bytes()!=b:
   old=r/'out/basic-bundle-previous-source-%s-20261108'/n
   assert not old.exists(),'A second unexpected source update requires review'
   old.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(t,old)
  t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(b)
print('Installed checked battery/touch source; prior files preserved')
'''%(version,digest,version)
installer=OUT/'install.py';installer.write_text(install,encoding='utf-8',newline='\n')
remote='ciscohe@192.168.68.54:/Users/ciscohe/Desktop/UniXNU_elden/out/'
subprocess.run(['scp',str(archive),remote+'basic-bundle-source-upload-'+version+'.zip'],check=True)
subprocess.run(['scp',str(installer),remote+'install_basic_bundle-'+version+'.py'],check=True)
subprocess.run(['ssh','-o','BatchMode=yes','ciscohe@192.168.68.54','cd /Users/ciscohe/Desktop/UniXNU_elden && .venv/bin/python out/install_basic_bundle-'+version+'.py'],check=True)
(OUT/'source-upload.json').write_text(json.dumps(dict(files=manifest,touch_matches_successful_build=proof,
    touch_reference_manifest=str(a.touch_manifest) if a.touch_manifest else str(snapshot/'source-manifest.json'),
    touch_baseline_unchanged=not bool(a.touch_manifest),
    recovery_source=str(a.recovery_source) if a.recovery_source else None,
    battery_archives='out/battery-telemetry-v'+str(a.power_version)+'-20261108',zip_sha256=digest,hardware_tested=False),indent=2)+'\n')
print(json.dumps(dict(source_files=len(manifest),matched_touch_files=len(proof),zip_sha256=digest),indent=2))
