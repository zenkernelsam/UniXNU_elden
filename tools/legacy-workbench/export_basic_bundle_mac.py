"""Freeze actual linked basic-driver sources and export the combined kernel."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
ROOT=Path('/Users/ciscohe/Desktop/UniXNU_elden')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--version',type=int,default=2);p.add_argument('--power-version',type=int,default=4)
p.add_argument('--backlight-version',type=int,default=0)
p.add_argument('--cpu-guard',action='store_true')
p.add_argument('--cpu-groups',action='store_true')
p.add_argument('--cpu-irq-snapshot',action='store_true')
a=p.parse_args();assert 2<=a.version<=99 and 4<=a.power_version<=99
assert 0<=a.backlight_version<=99
OUT=ROOT/('out/startup-basic-bundle-v'+str(a.version)+'-20261108')
TREE=ROOT/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
COMPILED=ROOT/('out/battery-telemetry-v'+str(a.power_version)+'-20261108')
assert sum((a.cpu_guard,a.cpu_groups,a.cpu_irq_snapshot))<=1
if a.cpu_guard or a.cpu_groups or a.cpu_irq_snapshot:
    assert (a.power_version,a.backlight_version)==(35,13)
    assert a.version in ((83,84,85,86,87) if a.cpu_irq_snapshot else (82,) if a.cpu_groups else (81,))
    TREE=ROOT/'research/cpu-guard-20261111/tree'
    COMPILED=ROOT/'research/cpu-guard-20261111/archives'
CPU_SOURCE=ROOT/('research/cpu-hold-20261111' if a.cpu_irq_snapshot and a.version==87 else 'research/cpu-asm-fixed-20261111' if a.cpu_irq_snapshot and a.version==86 else 'research/cpu-sgi-mode-20261111' if a.cpu_irq_snapshot and a.version==85 else 'research/cpu-mmio-read-20261111' if a.cpu_irq_snapshot and a.version==84 else 'research/cpu-irq-snapshot-20261111' if a.cpu_irq_snapshot else 'research/cpu-groups-20261111' if a.cpu_groups else 'research/cpu-guard-20261111')
UFS_SOURCE=CPU_SOURCE/'ufs/src' if a.cpu_groups or a.cpu_irq_snapshot else TREE/'build/sheng-ufs/src'
sha=lambda b:hashlib.sha256(b).hexdigest()
report=json.loads((TREE/'build/native-legacy-display/build-report.json').read_text())
assert report['physical_touch_linked'] and 'ELDEN_WIFI_FW_CHECK_BUILD' in report['selected_owned_features']
sources={};generated={}
for name,digest in report['source_sha256'].items():
    p=(TREE/name).resolve();assert p.is_relative_to(ROOT.resolve()) and p.is_file(),str(p);data=p.read_bytes()
    assert sha(data)==digest,name
    if name.endswith('EldenNvtFirmware.h'):generated[name]=digest
    else:sources['tree/'+name]=data
for p in (ROOT/('research/battery-telemetry-20261108/source-input-v'+str(a.power_version))).iterdir():
    if p.is_file():sources['power/'+p.name]=p.read_bytes()
if a.backlight_version:
    BACKLIGHT=ROOT/f'out/backlight-runtime-v{a.backlight_version}-20261109'
    if a.cpu_guard or a.cpu_groups or a.cpu_irq_snapshot:BACKLIGHT=COMPILED/'EldenBacklight'
    backlight=json.loads((BACKLIGHT/'build.json').read_text())
    assert sha(Path(backlight['archive']).read_bytes())==backlight['archive_sha256']
    for name,digest in backlight['source_files'].items():
        data=(ROOT/f'research/backlight-elden-20261109/source-input-v{a.backlight_version}'/name).read_bytes()
        assert sha(data)==digest; sources['backlight/'+name]=data
    for name in ('OriginalBacklightABI.h','original-bindings.S'):
        sources['backlight-generated/'+name]=(BACKLIGHT/name).read_bytes()
    sources['backlight-generated/compile.py']=(ROOT/f'out/compile_backlight_runtime-v{a.backlight_version}.py').read_bytes()
    for name,digest in backlight['objects'].items():assert sha((BACKLIGHT/name).read_bytes())==digest
    shutil.copyfile(BACKLIGHT/'abi-audit.json',OUT/'backlight-abi.json')
if a.cpu_guard or a.cpu_groups or a.cpu_irq_snapshot:
    script='relink_cpu_groups_mac.py' if a.cpu_groups or a.cpu_irq_snapshot else 'build_cpu_guard_mac.py'
    sources['build-scripts/'+script]=(CPU_SOURCE/script).read_bytes()
    sources['cpu/ArmCpu.cpp']=((CPU_SOURCE/'ArmCpu.cpp') if a.cpu_groups or a.cpu_irq_snapshot else TREE/'drivers/ArmCpu/ArmCpu.cpp').read_bytes()
    for name in ('GuardPolicy.h','SmpGuard.inc','Deferred.py'):
        sources['cpu/'+name]=(CPU_SOURCE/name).read_bytes()
    if a.version==87:
        for name in ('HoldPolicy.h','TestHoldPolicy.c'):sources['cpu/'+name]=(CPU_SOURCE/name).read_bytes()
    if a.cpu_groups:sources['cpu/GicGroups.h']=(CPU_SOURCE/'GicGroups.h').read_bytes()
    if a.cpu_irq_snapshot and a.version==85:sources['cpu/SgiMode.h']=(CPU_SOURCE/'SgiMode.h').read_bytes()
    for name in ('pexpert/pexpert/arm64/board_config.h','pexpert/pexpert/arm64/BCM2837.h','osfmk/arm64/machine_routines.c','osfmk/arm64/start.s','osfmk/kern/sched_clutch.c'):
        sources['cpu/xnu/'+name]=(TREE/'.cache/xnu-work'/name).read_bytes()
else:
    name='build_basic_bundle-v'+str(a.version)+'.py';sources['build-scripts/'+name]=(ROOT/'out'/name).read_bytes()
sources['build-scripts/prepare_battery_telemetry_derivative.py']=(ROOT/'out/prepare_battery_telemetry_derivative.py').read_bytes()
if a.version>=72:
    sources['build-scripts/build-xnu-4k-baseline.sh']=(TREE/'scripts/build-xnu-4k-baseline.sh').read_bytes()
commands=json.loads((TREE/'build/sheng-ufs/compile-command.json').read_text())
if a.cpu_groups or a.cpu_irq_snapshot:commands=json.loads((CPU_SOURCE/'result.json').read_text())['objects']['ufs']['command']
ma=next(int(s.split('=',1)[1]) for s in commands if s.startswith('-DELDEN_USB_CONFIG_MAX_MA='))
assert ma in (100,500)
for name in ['Dwc3.c','Dwc3.h','Dwc3Regs.h','EldenUsbDebugXnu.inc']:
    sources['usb/'+name]=(UFS_SOURCE/name).read_bytes()
if '-DELDEN_THERMAL_RUNTIME=1' in commands or '-DELDEN_TOUCH_RUNTIME=1' in commands:
    sources['usb/EldenSdamRecovery.inc']=(UFS_SOURCE/'EldenSdamRecovery.inc').read_bytes()
    if a.version>=39:
        sources['usb/EldenRebootOwnership.h']=(UFS_SOURCE/'EldenRebootOwnership.h').read_bytes()
if a.backlight_version:
    assert '-DELDEN_BACKLIGHT_RUNTIME=1' in commands
    sources['backlight-generated/EldenPowerStartup.inc']=(UFS_SOURCE/'EldenPowerStartup.inc').read_bytes()
sources['usb/build-sheng-ufs-probe.py']=(TREE/'scripts/build-sheng-ufs-probe.py').read_bytes()
executive=TREE/'.cache/xnu-work/bsd/kern/kern_exec.c'
if b'elden_icon_cpu_prepare(p,' in executive.read_bytes():
    for name in ('EldenIconCpuEnv.inc','EldenIconCpuStage.inc','EldenIconCpuPayload.h'):
        data=(executive.parent/name).read_bytes();assert data==(ROOT/'patches/xnu'/name).read_bytes()
        sources['xnu/'+name]=data
    sources['xnu/elden-icon-cpu.patch']=(ROOT/'patches/xnu/elden-icon-cpu.patch').read_bytes()
    sources['xnu/kern_exec.c']=executive.read_bytes()
    sources['package/EldenIconCpuMode.c']=(ROOT/'package/EldenIconCpuMode.c').read_bytes()
    if b'#include "EldenIconCpuCommon.inc"' in executive.read_bytes():
        for name in ('EldenIconCpuCommon.inc','EldenIconCpuPaths.h'):
            data=(executive.parent/name).read_bytes();assert data==(ROOT/'patches/xnu'/name).read_bytes();sources['xnu/'+name]=data
        sources['package/EldenIconCpuPaths.h']=(ROOT/'package/EldenIconCpuPaths.h').read_bytes()
        debug=executive.parent/'kdebug.c'
        if b'elden_icon_cpu_user_marker(p,' in debug.read_bytes():
            sources['xnu/kdebug.c']=debug.read_bytes()
            if b'#include "EldenUiDispatch.inc"' in debug.read_bytes():
                for name in ('EldenUiDispatch.inc','EldenUiDispatchCore.h'):
                    data=(executive.parent/name).read_bytes()
                    assert data==(ROOT/'patches/xnu'/name).read_bytes()
                    sources['xnu/'+name]=data
                sources['xnu/elden-ui-dispatch.patch']=(ROOT/'patches/xnu/elden-ui-dispatch.patch').read_bytes()
            exiting=executive.parent/'kern_exit.c'
            if b'elden_icon_cpu_exit_selected' in exiting.read_bytes():sources['xnu/kern_exit.c']=exiting.read_bytes()
            for name in ('bind_vfsops.c','bind_vnops.c','bindfs.h'):
                sources['xnu/bindfs/'+name]=(TREE/'.cache/xnu-work/bsd/miscfs/bindfs'/name).read_bytes()
for name in ('build-elden-gop.py','build-elden-hid.py'):
    sources['build-scripts/'+name]=(TREE/'scripts'/name).read_bytes()
core=(ROOT/'research/qcom-usb-core-20261107/usb/Dwc3.c').read_text()
if ma==500:
    old='9, dtype, 67, 0, 2, 1, 0, 0x80, 50,';assert core.count(old)==1
    core=core.replace(old,old.replace('0x80, 50,','0x80, 250,'))
assert sources['usb/Dwc3.c'].decode()==core
(OUT/'usb-budget.json').write_text(json.dumps(dict(usb_config_max_ma=ma,exact_descriptor_delta=True,
    usb_source_sha256={n:sha(b) for n,b in sources.items() if n.startswith('usb/')}),indent=2)+'\n')
manifest=dict(files={n:sha(b) for n,b in sources.items()},generated_firmware_headers=generated,hardware_tested=False)
with zipfile.ZipFile(OUT/'compiled-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n,b in sources.items():z.writestr(n,b)
    z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
for src,dest in [('build/sheng-hid-input/abi-audit.json','hid-abi.json'),
                 ('build/sheng-hid-input/touch-board-patch.json','touch-board-patch.json'),
                 ('build/native-legacy-display/touch-firmware-input.json','touch-firmware-input.json'),
                 ('build/native-legacy-display/build-report.json','display-build-report.json')]:
    shutil.copyfile(TREE/src,OUT/dest)
files={'kernel.macho':OUT/'entry/probe/kernel.macho'}
for n in ['kernel.json','result.json','derivative.json','compiled-source.zip','hid-abi.json',
          'touch-board-patch.json','touch-firmware-input.json','display-build-report.json','usb-budget.json']:
    files[n]=OUT/n
files['compile-result.json']=COMPILED/'compile-result.json'
if a.cpu_groups or a.cpu_irq_snapshot:
    files['cpu-irq-snapshot-result.json' if a.cpu_irq_snapshot else 'cpu-groups-result.json']=CPU_SOURCE/'result.json'
    for group,name in [('armcpu','ArmCpu'),('ufs','ShengUfs')]:
        for f in (CPU_SOURCE/group/(name+'.o'),CPU_SOURCE/group/('lib'+name+'.a')):
            files['cpu-archives/'+f.name]=f
if a.backlight_version:
    files['backlight-abi.json']=OUT/'backlight-abi.json'
    for p in BACKLIGHT.iterdir():
        if p.is_file() and p.suffix in ('.json','.a','.o'):files['archives/EldenBacklight/'+p.name]=p
for group in ['EldenAdspRuntime','EldenPmicClient']:
    for p in (COMPILED/group).iterdir():
        if p.is_file() and p.suffix in ('.json','.a','.o'):files['archives/'+group+'/'+p.name]=p
records=dict(files={n:sha(p.read_bytes()) for n,p in files.items()},source_files=len(sources),hardware_tested=False)
with zipfile.ZipFile(OUT/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n,p in files.items():z.write(p,n)
    z.writestr('artifact-hashes.json',json.dumps(records,indent=2)+'\n')
print(json.dumps(dict(kernel_sha256=records['files']['kernel.macho'],source_files=len(sources),
    zip_sha256=sha((OUT/'artifacts.zip').read_bytes()),zip_bytes=(OUT/'artifacts.zip').stat().st_size),indent=2))
