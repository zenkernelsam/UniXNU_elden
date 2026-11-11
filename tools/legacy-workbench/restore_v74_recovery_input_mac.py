from pathlib import Path
import hashlib,shutil
r=Path('/Users/ciscohe/Desktop/UniXNU_elden')
p=r/'scripts/driver-patches/EldenSdamRecovery.inc'
old=r/'out/basic-bundle-previous-source-v74-20261108/scripts/driver-patches/EldenSdamRecovery.inc'
assert hashlib.sha256(p.read_bytes()).hexdigest()=='3c3c39b4e82ef7cc2bb95ea0a5e49b6b72e4787a793caf7f758bcfd243e587bf'
assert hashlib.sha256(old.read_bytes()).hexdigest()=='8a0796b60bae23730ed92de2b1fc9d1a7b68a0a10e7d5efb87222ee820df12ba'
backup=r/'out/v74-reviewed-recovery-input.inc'
assert not backup.exists()
shutil.copyfile(p,backup);shutil.copyfile(old,p)
print('Restored exact v72 recovery input; displaced optional hook preserved.')
