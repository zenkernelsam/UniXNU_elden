"""Execute both real BootShim paths and verify the single RAM mailbox write."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn import arm64_const as r
from verify_offline_ufs import v

root=Path(__file__).resolve().parents[1]
work=root/'edl_work/rescue-compressed-20261106'
shim=(work/'shim-fdt-mailbox.bin').read_bytes()
legacy=(work/'shim-legacy.bin').read_bytes()
fd=(root/'edl_work/offline-20261105/old-fd.bin').read_bytes()
signed=(root/'edl_work/rootcheck-entry-20261106/boot-signed.img').read_bytes()
blob=gzip.decompress(signed[4096:4096+struct.unpack_from('<I',signed,8)[0]])
assert legacy==blob[:128] and len(shim)==128
v.verify_embedded_shim(shim,56*1024*1024)
rejected=[]
base_instruction=next(i for i in range(64,len(legacy)-8,4) if struct.unpack_from('<I',legacy,i)[0]==0x91500022)
header_only=bytearray(legacy)
struct.pack_into('<I',header_only,base_instruction+4,0xD2A01003)  # movz x3, #0x80, lsl #16 = 8 MiB
wrong_slot=bytearray(shim)
store=wrong_slot.find(struct.pack('<I',0xF900E840))
assert store>=0
struct.pack_into('<I',wrong_slot,store,0xF900EC40)  # neighboring, unapproved mailbox store
for name,bad in (('header-size-with-old-8MiB-x3',header_only),('wrong-FDT-slot',wrong_slot)):
    try:v.verify_embedded_shim(bytes(bad),56*1024*1024)
    except SystemExit:rejected.append(name)
    else:raise AssertionError('invalid shim was accepted: '+name)
sector=bytearray([0xa5]*4096)
sector[0x1c0:0x1d8]=b'ELDFDT01'+struct.pack('<IIQ',1,0,0)
results=[]
for in_place in (False,True):
    for fdt in (0,0xb0000000,0xfffffffffffe0000):
        target=0xff300000
        base=target-128 if in_place else 0xc2400000
        data=shim+fd+sector
        u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        begin=base&~4095;end=(base+len(data)+4095)&~4095
        u.mem_map(begin,end-begin);u.mem_write(base,bytes(data))
        if not in_place:u.mem_map(target,len(fd))
        u.reg_write(r.UC_ARM64_REG_X0,fdt)
        u.emu_start(base,target,count=1500000)
        assert u.reg_read(r.UC_ARM64_REG_PC)==target
        payload=base+128+len(fd)
        assert [u.reg_read(reg) for reg in (r.UC_ARM64_REG_X0,r.UC_ARM64_REG_X1,r.UC_ARM64_REG_X2,r.UC_ARM64_REG_X3)]==[fdt,base+128,payload,56*1024*1024]
        assert bytes(u.mem_read(target,len(fd)))==fd
        expected=bytearray(sector);struct.pack_into('<Q',expected,0x1d0,fdt)
        assert bytes(u.mem_read(payload,len(sector)))==expected
        results.append(dict(in_place=in_place,fdt_argument=hex(fdt),fd_copy_equal=True,
                            only_mailbox_changed=True,fdt_not_dereferenced=True))
report=dict(shim_sha256=hashlib.sha256(shim).hexdigest(),legacy_build_unchanged=True,
            bytes=len(shim),cases=results,negative_cases_rejected=rejected,device_tested=False)
(work/'shim-mailbox-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
