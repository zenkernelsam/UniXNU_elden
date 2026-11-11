"""Extract the exact UniXNU application from the preserved accepted old FD."""
from pathlib import Path
import hashlib
import json
import struct
import uuid
from verify_offline_ufs import v

root=Path(__file__).resolve().parents[1]
source=root/'edl_work/offline-20261105/old-fd.bin'
fd=source.read_bytes()
assert hashlib.sha256(fd).hexdigest()=='e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534'
fv=v.decompress_fvmain(fd)
target=uuid.UUID('9b665204-a133-47a2-9396-9e2b370a4e25').bytes_le
pos=(struct.unpack_from('<H',fv,48)[0]+7)&~7
found=[]
while pos+24<=len(fv) and fv[pos:pos+24]!=b'\xff'*24:
    size=int.from_bytes(fv[pos+20:pos+23],'little')
    assert size>=24 and pos+size<=len(fv)
    if fv[pos:pos+16]==target:
        at=pos+24
        while at+4<=pos+size:
            length=int.from_bytes(fv[at:at+3],'little')
            assert 4<=length and at+length<=pos+size
            if fv[at+3]==16:found.append(fv[at+4:at+length])
            at=(at+length+3)&~3
    pos=(pos+size+7)&~7
assert len(found)==1
data=found[0];pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[:2]==b'MZ' and data[pe:pe+4]==b'PE\0\0'
assert struct.unpack_from('<H',data,pe+4)[0]==0xaa64
assert struct.unpack_from('<H',data,pe+24+68)[0]==10
out=root/'edl_work/ufs-stage2-20261106/UniXNU-accepted.efi'
out.write_bytes(data)
report=dict(path=str(out),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
            source_fd_sha256=hashlib.sha256(fd).hexdigest(),guid=str(uuid.UUID(bytes_le=target)),
            executable_rebuilt=False,external_launch_tested=False)
out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
