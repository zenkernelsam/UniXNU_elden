"""Offline section rounding, real stock arithmetic, and dump admission tests."""
import hashlib
import json
from pathlib import Path
import random
import struct
import zlib
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X19,UC_ARM64_REG_X23,UC_ARM64_REG_W21,UC_ARM64_REG_W22
from f2fs_shrink_plan import plan_counts,parse_superblocks,MAGIC,CRC_OFFSET

root=Path(__file__).resolve().parents[1]
image=(root/'edl_work/rescue-compressed-20261106/Image.efi').read_bytes()
assert hashlib.sha256(image).hexdigest()=='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05'
example=plan_counts(57685263,40907796,512)
assert example['request_blocks']==40907264 and example['predicted_final_blocks']==40907535
assert example['naive_result_blocks']>example['new_mapping_capacity_blocks']
rng=random.Random(61106);cases=[]
for _ in range(5000):
    section=512*rng.randrange(1,129)
    old=section*rng.randrange(100,10000)+rng.randrange(section)
    capacity=rng.randrange(2*section,old-section)
    p=plan_counts(old,capacity,section)
    assert p['predicted_final_blocks']<=capacity
    assert p['predicted_final_blocks']+section>capacity
    assert old-((old-p['request_blocks'])//section)*section==p['predicted_final_blocks']
    cases.append(p)
# Run the original machine arithmetic after its lock, before any helper call.
# Only synthetic superblock words are changed; no filesystem is mounted.
native=[]
for p in [example,*cases[:20]]:
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0x5cb000,4096);u.mem_write(0x5cb000,image[0x5cb000:0x5cc000])
    raw=0x10000000;sbi=0x10002000;u.mem_map(raw,0x4000)
    segs_per_section=p['section_blocks']//512;removed=p['removed_sections']
    u.mem_write(raw+0x24,struct.pack('<Q',p['old_blocks']))
    u.mem_write(raw+0x2c,struct.pack('<II',100000,20000000))
    u.mem_write(raw+0x44,struct.pack('<I',20000000))
    u.mem_write(sbi+0x6ec,struct.pack('<I',9))
    u.reg_write(UC_ARM64_REG_X23,raw);u.reg_write(UC_ARM64_REG_X19,sbi)
    u.reg_write(UC_ARM64_REG_W21,(-removed)&0xffffffff)
    u.reg_write(UC_ARM64_REG_W22,(-removed*segs_per_section)&0xffffffff)
    u.emu_start(0x5cb098,0x5cb0c8,count=50)
    actual=struct.unpack('<Q',u.mem_read(raw+0x24,8))[0]
    assert actual==p['predicted_final_blocks']
    native.append(dict(old=p['old_blocks'],request=p['request_blocks'],actual=actual,capacity=p['new_mapping_capacity_blocks']))

superblock=bytearray(3072)
def put(offset,value):struct.pack_into('<I',superblock,offset,value)
for offset,value in [(0,MAGIC),(8,12),(12,0),(16,12),(20,9),(24,1),(28,1),(32,CRC_OFFSET),
                      (44,100),(48,110),(52,2),(56,2),(60,2),(64,4),(68,100),(92,6144),(2180,0x800)]:put(offset,value)
struct.pack_into('<Q',superblock,36,6144+100*512+83)
# Independent reflected CRC32 recurrence (kernel crc32_le seed, no final xor).
crc=MAGIC
for byte in superblock[:CRC_OFFSET]:
    crc^=byte
    for _ in range(8):crc=(crc>>1)^(0xedb88320 if crc&1 else 0)
assert crc==(zlib.crc32(superblock[:CRC_OFFSET],MAGIC^0xffffffff)^0xffffffff)&0xffffffff
put(CRC_OFFSET,crc)
dump=bytearray(8192);dump[1024:4096]=superblock;dump[5120:8192]=superblock
assert parse_superblocks(dump)['old_blocks']==6144+100*512+83
bad=bytearray(dump);bad[1024]^=1
try:parse_superblocks(bad);raise AssertionError('unequal superblocks accepted')
except ValueError:pass
bad=bytearray(dump);bad[1200]^=1;bad[5296]^=1
try:parse_superblocks(bad);raise AssertionError('bad CRC accepted')
except ValueError:pass
report=dict(random_rounding_cases=len(cases),actual_stock_arithmetic_cases=len(native),
            known_naive_overrun=example,disagreeing_superblocks_rejected=True,CRC_rejection=True,
            hardware_tested=False,filesystem_resize_executed=False)
(root/'edl_work/stock-f2fs-20261106/shrink-plan-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
