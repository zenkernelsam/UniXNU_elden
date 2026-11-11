from pathlib import Path
import hashlib,shutil
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');target=r/'patches/xnu/EldenUiDispatch.inc';source=r/'out/EldenUiDispatch-v75-scalar.inc'
assert hashlib.sha256(target.read_bytes()).hexdigest()=='1d7bda2539f8cab79edd6a3cfd008dfa1cd00b405c41a9c8be968e730b1b7608'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='d6caae47b41e176ff619e15659941bbaf1e699a47010511a0c175f07e03a2227'
backup=r/'out/EldenUiDispatch-v75-before-filter.inc';assert not backup.exists()
shutil.copyfile(target,backup);shutil.copyfile(source,target)
print('Reviewed scalar-only UI filter source installed; previous draft preserved.')
