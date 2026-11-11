"""WSL: preserve a byte-identical, licensed copy of the existing kShimC core."""
from pathlib import Path
import hashlib
import json
import shutil

source=Path('/root/kshimc')
target=Path('/mnt/d/Desktop/Git/UniXNU_elden/drivers/EldenUfsStage2')
mapping={
 'src/drivers/storage_ufs.c':'Core/storage_ufs.c',
 'src/core/storage_block.c':'Core/storage_block.c',
 'src/include/storage_ufs.h':'Core/storage_ufs.h',
 'src/include/storage_block.h':'Core/storage_block.h',
 'LICENSE':'COPYING',
 'tests/storage_ufs_test.c':'Tests/original_storage_ufs_test.c',
 'tests/storage_ufs_link_test.c':'Tests/original_storage_ufs_link_test.c',
 'tests/storage_ufs_power_test.c':'Tests/original_storage_ufs_power_test.c',
 'tests/storage_block_test.c':'Tests/original_storage_block_test.c',
}
record={}
for name,destination in mapping.items():
    data=(source/name).read_bytes()
    out=target/destination
    if out.exists() and out.read_bytes()!=data:raise SystemExit('refusing to overwrite changed vendor source '+str(out))
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
    record[destination]=dict(source=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
(target/'provenance.json').write_text(json.dumps(dict(source='existing /root/kshimc exported work tree',
    git_revision=None,license='GPL-3.0-only',files=record),indent=2)+'\n')
print('Preserved',len(record),'files; original work tree unchanged')
