"""WSL: independent C/Python format/hash and interrupted-update checks."""
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib
from stage2_store import Bundle, FileStore, MIB, decode, encode, inventory, transaction

ROOT=Path('/mnt/d/Desktop/Git/UniXNU_elden/drivers/EldenUfsStage2')
OUT=Path(__file__).resolve().parent/'ufs-stage2-20261106'
OUT.mkdir(exist_ok=True)
subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT),str(ROOT/'EldenBundle.c'),str(ROOT/'Core/storage_block.c'),
                '-o',str(OUT/'bundle.so')],check=True)
lib=ctypes.CDLL(str(OUT/'bundle.so'))
class CBundle(ctypes.Structure):
    _fields_=[('generation',ctypes.c_uint64),('bytes',ctypes.c_uint64),('span',ctypes.c_uint64),
              ('offset',ctypes.c_uint64),('slot',ctypes.c_uint32),('sha256',ctypes.c_uint8*32)]
lib.elden_bundle_sha256.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
lib.elden_bundle_header.argtypes=[ctypes.c_void_p,ctypes.c_uint64,ctypes.c_uint,ctypes.POINTER(CBundle)]
lib.elden_bundle_verify.argtypes=[ctypes.POINTER(CBundle),ctypes.c_void_p,ctypes.c_size_t]
rng=random.Random(61106)
sizes=[0,1,3,55,56,57,63,64,65,119,120,127,128,1000000]+[rng.randrange(2048) for _ in range(100)]
for size in sizes:
    data=rng.randbytes(size);digest=(ctypes.c_uint8*32)()
    lib.elden_bundle_sha256(data,len(data),digest)
    assert bytes(digest)==hashlib.sha256(data).digest(),size
data=rng.randbytes(MIB)
base=encode(Bundle(0,1,len(data),64*MIB,hashlib.sha256(data).digest()))
cases=[base,bytes(4096),base[:-1]]
for index in [0,8,12,16,24,64,72,76,80,4091]:
    h=bytearray(base);h[index]^=1;struct.pack_into('<I',h,4092,zlib.crc32(h[:-4]));cases.append(bytes(h))
for _ in range(500):
    h=bytearray(base)
    for _ in range(rng.randrange(1,8)):h[rng.randrange(4096)]^=rng.randrange(1,256)
    if rng.choice([False,True]):struct.pack_into('<I',h,4092,zlib.crc32(h[:-4]))
    cases.append(bytes(h))
for h in cases:
    if len(h)!=4096:continue # C API contract takes a full block.
    c=CBundle();rc=lib.elden_bundle_header(h,128*MIB,0,ctypes.byref(c))
    p=decode(h,128*MIB,0)
    assert (rc==0)==bool(p)
    if p:
        assert (c.generation,c.bytes,c.span,c.offset,c.slot,bytes(c.sha256))==(p.generation,p.size,p.span,p.offset,p.slot,p.digest)
c=CBundle();assert not lib.elden_bundle_header(base,128*MIB,0,ctypes.byref(c))
assert not lib.elden_bundle_verify(ctypes.byref(c),data,len(data))
assert lib.elden_bundle_verify(ctypes.byref(c),bytes(MIB),MIB)

READ=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint64,ctypes.c_size_t,ctypes.c_void_p)
ALLOCATE=ctypes.CFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t)
FREE=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_void_p)
class Reader(ctypes.Structure):
    _fields_=[('context',ctypes.c_void_p),('read',READ),('allocate',ALLOCATE),('free',FREE)]
lib.elden_bundle_load.argtypes=[ctypes.POINTER(Reader),ctypes.c_uint64,ctypes.c_int,
                                 ctypes.POINTER(CBundle),ctypes.POINTER(ctypes.c_void_p)]
def select_case(generations,previous=0,corrupt=(),read_fail=0,no_memory=False,expected=0,want=None):
    blocks={};allocations={};calls=[]
    for slot,generation in enumerate(generations):
        payload=bytes([slot+1])*MIB
        blocks[slot*64*MIB]=encode(Bundle(slot,generation,MIB,64*MIB,hashlib.sha256(payload).digest())) if generation else bytes(4096)
        blocks[slot*64*MIB+MIB]=b'x'*MIB if slot in corrupt else payload
    @READ
    def read(_,offset,size,dest):
        calls.append((offset,size))
        if len(calls)==read_fail:return -1
        if offset not in blocks or len(blocks[offset])!=size:return -1
        ctypes.memmove(dest,blocks[offset],size);return 0
    @ALLOCATE
    def allocate(_,size):
        if no_memory:return None
        value=ctypes.create_string_buffer(size);address=ctypes.addressof(value)
        allocations[address]=value;return address
    @FREE
    def release(_,value):
        assert value in allocations
        del allocations[value]
    reader=Reader(None,read,allocate,release);selected=CBundle();pointer=ctypes.c_void_p()
    rc=lib.elden_bundle_load(ctypes.byref(reader),128*MIB,previous,ctypes.byref(selected),ctypes.byref(pointer))
    assert rc==expected,(generations,previous,corrupt,rc,expected)
    if expected==0:
        assert selected.generation==want and pointer.value in allocations
        assert hashlib.sha256(ctypes.string_at(pointer,selected.bytes)).digest()==bytes(selected.sha256)
        release(None,pointer.value)
    else:assert not pointer.value
    assert not allocations,'failed selection leaked a buffer'
    if read_fail:assert len(calls)==read_fail,'continued reading after transport failure'

selection_cases=[
    dict(generations=(0,0),expected=-2),dict(generations=(1,0),want=1),
    dict(generations=(1,0),previous=1,expected=-2),dict(generations=(1,2),want=2),
    dict(generations=(1,2),previous=1,want=1),dict(generations=(3,2),want=3),
    dict(generations=(1,2),corrupt=(1,),want=1),dict(generations=(1,2),corrupt=(0,1),expected=-6),
    dict(generations=(1,2),previous=1,corrupt=(0,),expected=-6),
    dict(generations=(1,1),expected=-3),dict(generations=(1,2),no_memory=True,expected=-4),
    *[dict(generations=(1,2),read_fail=i,expected=-5) for i in (1,2,3)],
    dict(generations=(1,2),corrupt=(1,),read_fail=4,expected=-5)]
for case in selection_cases:select_case(**case)

class CrashStore(FileStore):
    def __init__(self,path,crash):super().__init__(path,True);self.crash=crash;self.operations=0
    def write(self,offset,data):
        self.operations+=1
        if self.operations==self.crash:
            super().write(offset,data[:len(data)//2]);super().sync()
            raise IOError('simulated torn write')
        super().write(offset,data)
    def sync(self):
        self.operations+=1
        if self.operations==self.crash:raise IOError('simulated lost sync')
        super().sync()

crashes=0
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'store.img'
    with path.open('wb') as f:f.truncate(128*MIB)
    s=FileStore(path,True)
    one=transaction(s,data,initialize=True,commit=True)
    assert one['generation']==1
    two=transaction(s,bytes(MIB),keep_generation=1,commit=True)
    assert two['generation']==2
    old=s.read(0,4096);olddata=s.read(MIB,MIB)
    slot1=s.read(64*MIB,4096);slot1data=s.read(65*MIB,MIB)
    s.close()
    for crash in range(1,8):
        # Restore both known complete local slots before each interruption.
        s=FileStore(path,True);s.write(0,old);s.write(MIB,olddata)
        s.write(64*MIB,slot1);s.write(65*MIB,slot1data);s.sync();s.close()
        s=CrashStore(path,crash)
        try:transaction(s,b'x'*MIB,keep_generation=1,commit=True)
        except IOError:crashes+=1
        finally:s.close()
        s=FileStore(path)
        _,_,valid=inventory(s)
        assert any(i.generation==1 for i in valid)
        assert s.read(0,4096)==old and s.read(MIB,MIB)==olddata
        s.close()
    s=FileStore(path,True)
    try:transaction(s,data,keep_generation=999,commit=True);raise AssertionError('accepted absent retained version')
    except ValueError:pass
    s.close()
report=dict(sha256_vectors=len(sizes),header_cross_checks=len(cases)-1,
            selection_fault_cases=len(selection_cases),torn_update_points=crashes,
            retained_slot_unchanged=True,hardware_tested=False)
(OUT/'bundle-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
