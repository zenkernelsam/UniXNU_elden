"""Create a separate Mac root-check archive tree; preserve the tested probe tree."""
from pathlib import Path
import shutil
import subprocess
import sys
import os

root = Path.cwd()
original = root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_probe/tree'
work = root/'out/ufs-rootcheck-tree'
work.mkdir(parents=True,exist_ok=True)
(work/'scripts/driver-patches').mkdir(parents=True,exist_ok=True)
(work/'build/sheng-ufs').mkdir(parents=True,exist_ok=True)
for dst, src in ((work/'.cache',original/'.cache'),(work/'build/xnu',original/'build/xnu'),
                 (work/'build/storage-abi',original/'build/storage-abi')):
    if not dst.exists():
        dst.symlink_to(src,target_is_directory=True)
source = work/'build/sheng-ufs/src'
if not source.exists():
    shutil.copytree(original/'build/sheng-ufs/src',source,symlinks=True)
shutil.copyfile(original/'components.toml',work/'components.toml')
shutil.copyfile(root/'scripts/build-sheng-ufs-probe.py',work/'scripts/build-sheng-ufs-probe.py')
for name in ('sheng-ufs-sm8750.patch','sheng-ufs-elden-rootcheck.patch','EldenUfsIdentity.h'):
    shutil.copyfile(root/'scripts/driver-patches'/name,work/'scripts/driver-patches'/name)
env=dict(os.environ)
env['XNU_ELDEN_UFS_ROOTCHECK']='1'
env['CCC_OVERRIDE_OPTIONS']='+-Wno-unknown-warning-option'
result = subprocess.run([sys.executable,'scripts/build-sheng-ufs-probe.py'],cwd=work,env=env,
                        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(root/'out/bringup-20261105/ufs-root-archive.log').write_text(result.stdout)
print(result.stdout[-6000:])
raise SystemExit(result.returncode)
