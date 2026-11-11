"""Extract the exact DarwinBoot PE from the accepted FD for offline testing."""
from pathlib import Path
import hashlib
import json
import struct
import uuid
from verify_offline_ufs import v

root=Path(__file__).resolve().parents[1]
fd=(root/'edl_work/offline-20261105/old-fd.bin').read_bytes()
assert hashlib.sha256(fd).hexdigest()=='e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534'
fv=v.decompress_fvmain(fd)
target=uuid.UUID('1b64af87-8415-4b57-9b16-680d70913b9f').bytes_le
pos=(struct.unpack_from('<H',fv,48)[0]+7)&~7
found=None
while pos+24<=len(fv) and fv[pos:pos+24]!=b'\xff'*24:
    size=int.from_bytes(fv[pos+20:pos+23],'little')
    assert size>=24 and pos+size<=len(fv)
    if fv[pos:pos+16]==target:
        at=pos+24
        while at+4<=pos+size:
            length=int.from_bytes(fv[at:at+3],'little')
            assert 4<=length and at+length<=pos+size
            if fv[at+3]==16:found=fv[at+4:at+length]
            at=(at+length+3)&~3
        break
    pos=(pos+size+7)&~7
assert found is not None
assert hashlib.sha256(found).hexdigest()=='b3c05c02ca9122d5f2cd1d855635148cbfdaa210b824e8a0a0fafb7867c5511b'
out=root/'edl_work/rescue-compressed-20261106/DarwinBoot.efi'
out.write_bytes(found)
print(json.dumps(dict(path=str(out),bytes=len(found),sha256=hashlib.sha256(found).hexdigest())))
