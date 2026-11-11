from pathlib import Path
import hashlib,subprocess
r=Path('/Users/ciscohe/Desktop/UniXNU_elden')
t=r/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/.cache/xnu-work'
p=r/'out/basic-bundle-previous-source-v76-20261108/patches/xnu/elden-ui-dispatch.patch'
assert hashlib.sha256((t/'bsd/kern/kdebug.c').read_bytes()).hexdigest()=='53fe2538d55c2222dac791425f98f1ccedffad65a2a77802c0de6d3eeaf13ad4'
assert p.is_file()
subprocess.run(['git','-C',str(t),'apply','--reverse','--check',str(p)],check=True)
subprocess.run(['git','-C',str(t),'apply','--reverse',str(p)],check=True)
print('Removed only the previous reviewed UI patch; remaining owner patches preserved.')
