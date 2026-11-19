"""Reuse the fixed original SSCPACK1 serializer, excluding archive/default code."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def serializer():
    record=json.loads((HERE/'pack-source.json').read_text());b=(HERE/'upstream/PaganiMakeSensorPack.py').read_bytes()
    if len(b)!=record['bytes'] or hashlib.sha256(b).hexdigest()!=record['sha256']:raise ValueError('Fixed sensor pack serializer changed')
    t=b.decode();start=t.index('def pack(files):');end=t.index('\ndef main():',start)
    selected=t[start:end];scope={'MAX_ENTRIES':8192,'MAX_NAMES':0x100000}
    import struct
    from pathlib import PurePosixPath
    def fail(message):raise ValueError(message)
    scope.update(struct=struct,PurePosixPath=PurePosixPath,fail=fail)
    exec(compile(selected,record['path'],'exec'),scope)
    return scope['pack'],record,hashlib.sha256(selected.encode()).hexdigest()
