"""Mac incremental owner-pinned root build using the qualified existing tree.

Preserves the accepted boot artifacts and active CLI configuration. Builds the
new UFS archive, relinks source XNU, and prelinks into a separate output folder.
"""
from pathlib import Path
import hashlib,json,os,shlex,shutil,subprocess,sys,time
root=Path.cwd();base=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados';tree=root/'research/cpu-full-20261111/tree'
out=root/'research/cpu-full-20261111/out';out.mkdir(exist_ok=True)
for name in ('kernel.macho','kernel.json'):
    source=base/'kernel'/name
    if not (out/('baseline-'+name)).exists():shutil.copyfile(source,out/('baseline-'+name))
assert hashlib.sha256((out/'baseline-kernel.macho').read_bytes()).hexdigest()=='7d50eae33ea7e89925ce6b475665201b64fbed5b19dba1f5699fab07aeeb0b5a'
for relative in ('scripts/driver-patches/sheng-ufs-elden-timeout-recheck.patch','scripts/build-sheng-ufs-probe.py','scripts/driver-patches/sheng-ufs-elden-pinned-root.patch',
                 'scripts/driver-patches/EldenPinnedRoot.h','scripts/driver-patches/EldenPinnedRootXnu.inc',
                 'scripts/driver-patches/EldenPinnedRootBridge.inc',
                 'scripts/driver-patches/EldenUsbDebugXnu.inc','scripts/driver-patches/sheng-ufs-elden-usb-debug.patch'):
    shutil.copyfile(root/relative,tree/relative)
env=dict(os.environ)
for line in (base/'.config').read_text().splitlines():
    if not line.startswith('CONFIG_') or '=' not in line:continue
    key,value=line.split('=',1);value=value.strip()
    env[key[7:]]={'y':'1','n':'0'}.get(value,value.strip('"'))
env.update(XNU_ELDEN_UFS_ROOTCHECK='1',XNU_ELDEN_UFS_ROOT_PARTITION='ipados',XNU_ELDEN_UFS_PINNED_ROOT='1',JOBS='4',CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
# The format-2 resolver already applied the ordered patch stack. The legacy
# fallback in the build script must not reverse/reapply it in glob order.
env['UNIXNU_PATCH_LIST']='format2-already-prepared'
env['XNU_ELDEN_USB_DEBUG']='1'
env['XNU_ELDEN_THERMAL_RUNTIME']='1'
env['XNU_ELDEN_TOUCH_RUNTIME']='1'
env['ELDEN_USB_CONFIG_MAX_MA']='500'
env['ELDEN_USB_RESOURCE_HEADER']=str(root/'soc/sm8750/include/EldenUsbResources.h')
env.update(XNU_OWNER_ROOT_AUDIT='1',XNU_OWNER_ROOT_TRUST='1',XNU_OWNER_CACHE_MOUNT='1',
           XNU_OWNER_CACHE_STARTUP='1',XNU_OWNER_BOOT_MANIFEST='1',XNU_OWNER_DATA_MOUNT='1',
           XNU_OWNER_UNION_SYSTEMOS='1',XNU_OWNER_CRYPTEX_FILES='0',XNU_NATIVE_RAM_BLOCK='0',
           XNU_OWNER_CACHE_VOLUME_UUID='8846FE78-2910-4297-AF97-D59BB6F8FC16')
review=root/'research/pinned-root-20261107'
hashes=json.loads((review/'build-script-hashes.json').read_text())
target=tree/'scripts/build-xnu-4k-baseline.sh'
assert hashlib.sha256(target.read_bytes()).hexdigest() in hashes.values()
assert hashlib.sha256((review/'build-xnu.owned.sh').read_bytes()).hexdigest()==hashes['patched']
shutil.copyfile(review/'build-xnu.owned.sh',target)
env['ELDEN_DWC3_SOURCE']=str(root/'research/qcom-usb-core-20261107/usb')
patch=root/'patches/xnu/elden-usb-console-hook.patch'
command=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(command+['--check',str(patch)],capture_output=True).returncode==0:
    subprocess.run(command+[str(patch)],check=True)
elif subprocess.run(command+['--reverse','--check',str(patch)],capture_output=True).returncode!=0:
    raise RuntimeError('USB console hook does not apply cleanly')
env['PATH']=str(root/'.venv/bin')+':'+str(root/'.cache/dtc-src/bin')+':'+env['PATH']
env['ELDEN_STORAGE_ABI_AUDIT']=str(root/'out/elden-storage-abi/build/storage-abi/interface-audit.json')
env['ELDEN_STORAGE_ABI_CANDIDATE']=str(root/'out/bringup-20261105/kernel-gop.macho')
env['ELDEN_STORAGE_ABI_PLAN']=str(root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_probe/kernel/kernel.json')
env['XNU_NATIVE_RAM_BLOCK']='1'
env['XNU_OWNER_DATA_VOLUME_UUID']='E5D06647-CCB4-491D-A897-3C21B55703CF'
env['ELDEN_RAM_BLOCK_REPOSITORY']=str(root/'third_party/UniXNU/drivers/RamBlock')
for name in ('build-elden-ram-block.py','build-elden-ufs-bindings.py'):
    shutil.copyfile(root/'scripts'/name,tree/'scripts'/name)
# The final observer adds context beside the metadata hook. Temporarily peel
# only this owned, fully reversible patch so prior-patch checks stay exact.
ui_patch=root/'patches/xnu/elden-ui-dispatch.patch'
ui_apply=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(ui_apply+['--reverse','--check',str(ui_patch)],capture_output=True).returncode==0:
    subprocess.run(ui_apply+['--reverse',str(ui_patch)],check=True)
icon_patch=root/'patches/xnu/elden-icon-cpu.patch'
icon_apply=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(icon_apply+['--reverse','--check',str(icon_patch)],capture_output=True).returncode==0:
    subprocess.run(icon_apply+['--reverse',str(icon_patch)],check=True)
last=root/'patches/xnu/elden-display-mode-observer.patch'
peel=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(peel+['--reverse','--check',str(last)],capture_output=True).returncode==0:
    subprocess.run(peel+['--reverse',str(last)],check=True)
for patch_name in ('elden-watchdog-launch-policy.patch','elden-panic-recovery.patch','elden-rpc-halt.patch','elden-userfault-metadata.patch','elden-usb-graphics-console.patch','elden-console-physical-marker.patch','elden-display-mode-observer.patch','elden-icon-cpu.patch','elden-ui-dispatch.patch'):
    patch=root/'patches/xnu'/patch_name
    command=['git','-C',str(tree/'.cache/xnu-work'),'apply']
    if subprocess.run(command+['--check',str(patch)],capture_output=True).returncode==0:
        subprocess.run(command+[str(patch)],check=True)
    elif subprocess.run(command+['--reverse','--check',str(patch)],capture_output=True).returncode!=0:
        raise RuntimeError('Diagnostic patch mismatch: '+patch_name)
shutil.copyfile(root/'patches/xnu/EldenWatchdogPolicy.inc',tree/'.cache/xnu-work/bsd/kern/EldenWatchdogPolicy.inc')
for name in ('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h','EldenIconCpuCommon.inc','EldenIconCpuPaths.h'):
    shutil.copyfile(root/'patches/xnu'/name,tree/'.cache/xnu-work/bsd/kern'/name)
env['XNU_ELDEN_SDAM_RECOVERY']='1'
env['ELDEN_REBOOT_RESOURCE_HEADER']=str(root/'soc/sm8750/include/EldenRebootResources.h')
for name in ('EldenRebootProbe.inc','EldenSpmiRestart.h','EldenSdamRecovery.inc','EldenRebootOwnership.h','sheng-ufs-elden-sdam-recovery.patch'):
    shutil.copyfile(root/'scripts/driver-patches'/name,tree/'scripts/driver-patches'/name)
env.update(XNU_NATIVE_LEGACY_DISPLAY='1',XNU_NATIVE_MOBILE_DISPLAY='0',
    ELDEN_GOP_REPOSITORY=str(root/'third_party/UniXNU/drivers/GopFramebuffer'),
    ELDEN_DISPLAY_CANDIDATE=str(root/'out/startup-userfault-meta-20261107/kernel.macho'),
    ELDEN_DISPLAY_PLAN=str(root/'out/startup-userfault-meta-20261107/kernel.json'),
    ELDEN_LLVM_OBJCOPY='/Users/ciscohe/toolchains/llvm-20/bin/llvm-objcopy')
for name in ('build-elden-gop.py','audit-elden-gop.py','driver-patches/EldenDisplayGlue.cpp','driver-patches/EldenUserWaitProbe.c','driver-patches/EldenKeyEventsGlue.cpp','driver-patches/EldenHidGlue.cpp','build-elden-hid.py'):
    shutil.copyfile(root/'scripts'/name,tree/'scripts'/name)
env['ELDEN_KEYEVENTS_REPOSITORY']=str(root/'third_party/UniXNU/drivers/AppleKeyStoreState')
env.update(ELDEN_HID_BUILD='1',ELDEN_HID_REPOSITORY=str(root/'third_party/UniXNU/drivers/ShengHidInput'),ELDEN_HID_CANDIDATE=str(root/'out/startup-services-20261107/kernel.macho'),ELDEN_HID_PLAN=str(root/'out/startup-services-20261107/kernel.json'),ELDEN_LLVM_BIN='/Users/ciscohe/toolchains/llvm-20/bin')
# Board power/runtime additions are separately compiled and admitted archives.
for filename in ('EldenModemReader.inc','EldenPowerStartup.inc'):
    shutil.copyfile(root/'scripts/driver-patches'/filename,tree/'scripts/driver-patches'/filename)
env.update(XNU_ELDEN_MODEM_READER='1',XNU_ELDEN_POWER_RUNTIME='1',
    ELDEN_MODEM_SOURCE=str(root/'drivers/EldenPmicGlink'),
    ELDEN_POWER_INSPECT_BUILD='0',ELDEN_TOUCH_INSPECT_BUILD='0')
paths=[root/'out/battery-telemetry-v35-20261108/EldenAdspRuntime/libEldenAdspRuntime.a',
       root/'out/battery-telemetry-v35-20261108/EldenPmicClient/libEldenPmicClient.a',
       root/'out/night-cores-20261108/EldenPmicTransport/libEldenPmicTransport.a']
for path in paths:
    report=json.loads((path.parent/'build.json').read_text())
    assert hashlib.sha256(path.read_bytes()).hexdigest()==report['archive_sha256']
    assert report['arm64_ios17'] and not report['automatic_initializers']
    assert ',' not in str(path) and not any(c.isspace() for c in str(path))
env['XNU_EXTRA_LD_ARCHIVES']=' '.join('-Wl,-force_load,'+str(path) for path in paths)

backlight=root/'out/backlight-runtime-v13-20261109'
backlight_report=json.loads((backlight/'build.json').read_text())
assert backlight_report['arm64_ios17'] and not backlight_report['automatic_initializers']
for name,digest in backlight_report['objects'].items():
    assert hashlib.sha256((backlight/name).read_bytes()).hexdigest()==digest
backlight_archive=Path(backlight_report['archive'])
assert hashlib.sha256(backlight_archive.read_bytes()).hexdigest()==backlight_report['archive_sha256']
env['XNU_EXTRA_LD_ARCHIVES']+=' -Wl,-force_load,'+str(backlight_archive)
env['XNU_ELDEN_BACKLIGHT_RUNTIME']='1'

env.update(ELDEN_BLUETOOTH_BUILD='0',ELDEN_BLUETOOTH_HCI_BUILD='0',ELDEN_BLUETOOTH_UART_BUILD='0',ELDEN_WCN_POWER_BUILD='0',ELDEN_BT_NATIVE_BUILD='0',ELDEN_TOUCH_RUNTIME_BUILD='0',ELDEN_TOUCH_HID_BUILD='0',ELDEN_HID_PHYSICAL_TOUCH='0',XNU_ELDEN_DATA_STORE='0',ELDEN_DATA_INSPECT_BUILD='0')

for folder in ('drivers/EldenNvt36536','research/qcom-geni-spi-20261107'):
    for src in (root/folder).iterdir():
        if src.is_file():
            dest=tree/folder/src.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
for name in ('package/EldenTouchBoard.h','soc/sm8750/include/EldenTouchResources.h',
             'research/nt36536-runtime-20261108/hid-owned-slots.json'):
    dest=tree/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,dest)
env.update(ELDEN_TOUCH_HID_BUILD='1',ELDEN_HID_PHYSICAL_TOUCH='1',ELDEN_TOUCH_RUNTIME_BUILD='1',
    ELDEN_TOUCH_INSPECT_BUILD='0',ELDEN_POWER_INSPECT_BUILD='0',
    XNU_ELDEN_MODEM_READER='1',XNU_ELDEN_POWER_RUNTIME='1',
    ELDEN_TOUCH_HID_ABI_SOURCE=str(tree/'research/nt36536-runtime-20261108/hid-owned-slots.json'),
    ELDEN_TOUCH_DRIVER_SOURCE=str(tree/'drivers/EldenNvt36536'),
    ELDEN_TOUCH_GENI_SOURCE=str(tree/'research/qcom-geni-spi-20261107'),
    ELDEN_TOUCH_BOARD_HEADER=str(tree/'package/EldenTouchBoard.h'),
    ELDEN_TOUCH_RESOURCE_HEADER=str(tree/'soc/sm8750/include/EldenTouchResources.h'),
    ELDEN_TOUCH_FIRMWARE_INPUT=str(root/'research/nt36536-runtime-20261108/novatek_ts_csot_3k_fw.bin'))

for folder in ('drivers/EldenWifi',):
    for src in (root/folder).iterdir():
        if src.is_file():
            dest=tree/folder/src.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
for name in ('package/EldenWifiFirmwareInputs.h','soc/sm8750/include/EldenWifiResources.h'):
    dest=tree/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/name,dest)
env.update(ELDEN_WIFI_FW_CHECK_BUILD='1',ELDEN_WIFI_SOURCE=str(tree/'drivers/EldenWifi'),
    ELDEN_WIFI_RESOURCE_HEADER=str(tree/'soc/sm8750/include/EldenWifiResources.h'),
    ELDEN_WIFI_INPUT_HEADER=str(tree/'package/EldenWifiFirmwareInputs.h'))

flag_source=(tree/'scripts/build-xnu-4k-baseline.sh').read_text()
flag_defaults=[line[len('EXTRA_CFLAGS=${XNU_EXTRA_CFLAGS:-'):-1] for line in flag_source.splitlines() if line.startswith('EXTRA_CFLAGS=${XNU_EXTRA_CFLAGS:-') and line.endswith('}')]
assert len(flag_defaults)==1
observer_flags=env.get('XNU_EXTRA_CFLAGS',flag_defaults[0])
assert 'XNU_ELDEN_ICON_OBSERVER' not in observer_flags
env['XNU_EXTRA_CFLAGS']=observer_flags+' -DXNU_ELDEN_ICON_OBSERVER=1'
env['XNU_EXTRA_CFLAGS']+=' -DXNU_ELDEN_UI_DISPATCH=1'
for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h'):
    shutil.copyfile(root/'patches/xnu'/name,tree/'.cache/xnu-work/bsd/kern'/name)

selected={k:v for k,v in env.items() if k.startswith(('XNU_','ELDEN_','CONFIG_')) or k in ('PATH','JOBS','CCC_OVERRIDE_OPTIONS','UNIXNU_PATCH_LIST')}
(root/'research/cpu-full-20261111/environment.json').write_text(json.dumps(selected,indent=2))
