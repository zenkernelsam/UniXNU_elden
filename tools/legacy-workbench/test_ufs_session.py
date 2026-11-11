"""WSL: verify unchanged imported core and the new session boundary tests."""
from pathlib import Path
import hashlib
import json
import subprocess

root=Path('/mnt/d/Desktop/Git/UniXNU_elden/drivers/EldenUfsStage2')
out=Path('/mnt/d/Desktop/Git/Patch/Y700-Elden/edl_work/ufs-stage2-20261106')
out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((root/'provenance.json').read_text())
for name,item in manifest['files'].items():
    assert hashlib.sha256((root/name).read_bytes()).hexdigest()==item['sha256'],name
tests=[('original_storage_ufs_test','Core/storage_ufs.c'),
       ('original_storage_ufs_link_test','Core/storage_ufs.c'),
       ('original_storage_ufs_power_test','Core/storage_ufs.c'),
       ('original_storage_block_test','Core/storage_block.c')]
report=[]
for name,source in tests+[('session_test','EldenUfsSession.c')]:
    sources=[root/'Tests'/(name+'.c'),root/source]
    if name=='session_test':sources += [root/'Core/storage_ufs.c',root/'Core/storage_block.c']
    elif source!='Core/storage_block.c':sources.append(root/'Core/storage_block.c')
    command=['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(root),'-I'+str(root/'Core'),*map(str,sources),'-o',str(out/name)]
    built=subprocess.run(command,text=True,capture_output=True)
    if built.returncode:print(built.stdout+built.stderr);raise SystemExit(built.returncode)
    result=subprocess.run([out/name],text=True,capture_output=True)
    report.append(dict(test=name,returncode=result.returncode,output=result.stdout+result.stderr))
    (out/'tests.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,result.returncode,result.stdout.strip(),flush=True)
    if result.returncode:raise SystemExit(result.returncode)
print('PASS: unchanged GPL core plus session-specific boundaries; no hardware access')
