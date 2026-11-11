"""Mac incremental owner-pinned root build using the qualified existing tree.

Preserves the accepted boot artifacts and active CLI configuration. Builds the
new UFS archive, relinks source XNU, and prelinks into a separate output folder.
"""
from pathlib import Path
import hashlib,json,os,shlex,shutil,subprocess,sys,time
root=Path.cwd();base=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados';tree=base/'tree'
out=root/'out/startup-root-r4-20261107';out.mkdir(exist_ok=True)
for name in ('kernel.macho','kernel.json'):
    source=base/'kernel'/name
    if not (out/('baseline-'+name)).exists():shutil.copyfile(source,out/('baseline-'+name))
assert hashlib.sha256((out/'baseline-kernel.macho').read_bytes()).hexdigest()=='7d50eae33ea7e89925ce6b475665201b64fbed5b19dba1f5699fab07aeeb0b5a'
for relative in ('scripts/build-sheng-ufs-probe.py','scripts/driver-patches/sheng-ufs-elden-pinned-root.patch',
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
progress=root/'patches/xnu/elden-owner-startup-log.patch'
cmd=['git','-C',str(tree/'.cache/xnu-work'),'apply']
if subprocess.run(cmd+['--check',str(progress)],capture_output=True).returncode==0:subprocess.run(cmd+[str(progress)],check=True)
elif subprocess.run(cmd+['--reverse','--check',str(progress)],capture_output=True).returncode!=0:raise RuntimeError('startup log patch mismatch')

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
log=out/'build.log';started=time.monotonic()
with log.open('w') as f:
    def run(command,cwd):
        f.write('+ '+shlex.join(map(str,command))+'\n');f.flush()
        subprocess.run(list(map(str,command)),cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    run([sys.executable,tree/'scripts/build-sheng-ufs-probe.py'],tree)
    run(['bash',tree/'scripts/build-xnu-4k-baseline.sh'],tree)
    command_line=next(x[2:] for x in (root/'out/ipados-console-20261107/build.log').read_text().splitlines() if x.startswith('+ ') and '/emit_prelink.py ' in x)
    command=shlex.split(command_line);command[0]=sys.executable;command[command.index('--output')+1]=str(out/'kernel.macho')
    run(command,root)
report=dict(kernel_sha256=hashlib.sha256((out/'kernel.macho').read_bytes()).hexdigest(),seconds=time.monotonic()-started,pinned_root=True,read_only=True,active_configuration_changed=False)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
