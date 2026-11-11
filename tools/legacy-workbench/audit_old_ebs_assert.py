"""Inspect the real accepted FD's VarPolicy assert, without running firmware."""
from pathlib import Path
import hashlib
import json
import struct
import uuid
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_IMM, ARM64_OP_MEM
from verify_offline_ufs import v

root = Path(__file__).resolve().parents[1]
work = root / 'edl_work/ebs-20261106'
work.mkdir(parents=True, exist_ok=True)
fd = (root / 'edl_work/offline-20261105/old-fd.bin').read_bytes()
assert hashlib.sha256(fd).hexdigest() == 'e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534'
fv = v.decompress_fvmain(fd)
target = uuid.UUID('8c7db881-da76-4486-8654-8152ee3470a3').bytes_le
pos = (struct.unpack_from('<H', fv, 48)[0] + 7) & ~7
pe = None
while pos + 24 <= len(fv) and fv[pos:pos+24] != b'\xff' * 24:
    size = int.from_bytes(fv[pos+20:pos+23], 'little')
    assert size >= 24 and pos + size <= len(fv)
    if fv[pos:pos+16] == target:
        cursor = pos + 24
        while cursor + 4 <= pos + size:
            length = int.from_bytes(fv[cursor:cursor+3], 'little')
            kind = fv[cursor+3]
            header = 4
            if length == 0xffffff:
                length = struct.unpack_from('<I', fv, cursor+4)[0]
                header = 8
            assert length >= header and cursor+length <= pos+size
            if kind == 16:
                pe = fv[cursor+header:cursor+length]
            cursor = (cursor+length+3) & ~3
        break
    pos = (pos+size+7) & ~7
assert pe is not None and hashlib.sha256(pe).hexdigest() == '06b6d0948c755f3faec20a975ea2fc94362d48509b009fdbbeb820f755ed8bbc'
(work / 'MuVarPolicyFoundationDxe-old.efi').write_bytes(pe)
nt = struct.unpack_from('<I', pe, 0x3c)[0]
assert pe[nt:nt+4] == b'PE\0\0' and struct.unpack_from('<H', pe, nt+4)[0] == 0xaa64
count, optional = struct.unpack_from('<H', pe, nt+6)[0], struct.unpack_from('<H', pe, nt+20)[0]
base = struct.unpack_from('<Q', pe, nt+24+24)[0]
sections = []
for i in range(count):
    off = nt+24+optional+i*40
    name = pe[off:off+8].rstrip(b'\0').decode()
    virtual_size, rva, raw_size, raw = struct.unpack_from('<4I', pe, off+8)
    flags = struct.unpack_from('<I', pe, off+36)[0]
    sections.append(dict(name=name, rva=rva, virtual_size=virtual_size, raw_size=raw_size, raw=raw, flags=flags))

def read(address, size):
    rva = address-base
    for section in sections:
        offset = rva-section['rva']
        if 0 <= offset and offset+size <= section['raw_size']:
            return pe[section['raw']+offset:section['raw']+offset+size]
    raise ValueError('unbacked PE address ' + hex(address))

md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.detail = True
md.skipdata = True
instructions = []
for section in sections:
    if section['flags'] & 0x20000000:
        instructions.extend(md.disasm(pe[section['raw']:section['raw']+section['raw_size']], base+section['rva']))
(work / 'old-varpolicy-disassembly.txt').write_text(''.join(
    f'{ins.address:08x} {ins.bytes.hex()} {ins.mnemonic} {ins.op_str}\n' for ins in instructions), encoding='utf-8')

def register(ins, ident):
    name = ins.reg_name(ident)
    return 'x'+name[1:] if name.startswith('w') else name

branches = []
for i, ins in enumerate(instructions):
    if ins.mnemonic not in ('tbz', 'tbnz') or ins.operands[1].imm not in (4, 5):
        continue
    known, loads = {}, {}
    for prior in instructions[max(0,i-12):i]:
        if prior.id == 0:
            known.clear(); loads.clear()
            continue
        ops = prior.operands
        if prior.mnemonic in ('bl', 'blr', 'b', 'br', 'ret'):
            known.clear(); loads.clear()
            continue
        _, written = prior.regs_access()
        old = dict(known)
        for ident in written:
            known.pop(register(prior,ident), None)
        if not ops or ops[0].type != ARM64_OP_REG:
            continue
        dest = register(prior,ops[0].reg)
        if prior.mnemonic in ('adrp','adr','mov') and len(ops)>1 and ops[1].type == ARM64_OP_IMM:
            known[dest] = ops[1].imm
        elif prior.mnemonic == 'add' and len(ops)==3 and ops[1].type == ARM64_OP_REG and ops[2].type == ARM64_OP_IMM:
            source = register(prior,ops[1].reg)
            if source in old:
                known[dest] = old[source]+ops[2].imm
        elif prior.mnemonic == 'ldrb' and ops[1].type == ARM64_OP_MEM:
            source = register(prior,ops[1].mem.base)
            if source in old and not ops[1].mem.index:
                address = old[source]+ops[1].mem.disp
                try:
                    known[dest] = read(address,1)[0]
                    loads[dest] = address
                except ValueError:
                    pass
    flag = register(ins,ins.operands[0].reg)
    branches.append(dict(address=hex(ins.address), mnemonic=ins.mnemonic, operands=ins.op_str,
        known_register_value=known.get(flag), byte_load_address=hex(loads[flag]) if flag in loads else None,
        context=[f'{p.address:x}: {p.mnemonic} {p.op_str}' for p in instructions[max(0,i-5):i+4]]))
record = dict(fd_sha256=hashlib.sha256(fd).hexdigest(), pe_sha256=hashlib.sha256(pe).hexdigest(),
    pe_bytes=len(pe), image_base=hex(base), inspected_original_fd=True, firmware_executed=False,
    sections=sections, instruction_count=len(instructions), branches=branches,
    assert_strings=[dict(file_offset=hex(i),text=pe[i:i+120].split(b'\0',1)[0].decode('ascii',errors='replace'))
        for i in range(len(pe)) if pe[i:i+6] == b'ASSERT'])
(work / 'old-assert-audit.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record,indent=2))
