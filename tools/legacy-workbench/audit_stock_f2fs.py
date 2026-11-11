"""Inspect the exact stock kernel and execute only its non-writing resize exits."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_SP_EL0, UC_ARM64_REG_PSTATE, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X30, UC_ARM64_REG_W0
from avb_guard import check_image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'edl_work/stock-f2fs-20261106'
data=(ROOT/'edl_work/rescue-compressed-20261106/Image.efi').read_bytes()
sha=hashlib.sha256(data).hexdigest()
assert sha=='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05'
boot_path=ROOT/'signing/phase5_resign/boot_signed.img'
boot=check_image(boot_path,'boot_a');container=boot_path.read_bytes()
assert container[4096:4096+struct.unpack_from('<I',container,8)[0]]==data
symbol_rows=[line.split('\t',1) for line in (OUT/'symbols.tsv').read_text().splitlines()]
symbols={name:int(offset,16) for offset,name in symbol_rows}
for name in ('_text','_stext','__f2fs_ioctl','f2fs_resize_fs'):
    assert sum(row[1]==name for row in symbol_rows)==1,'ambiguous critical symbol'
assert len(symbols)>100000 and symbols['_text']==0 and symbols['_stext']==0x10000
assert symbols['f2fs_resize_fs']==0x5ca8ac and symbols['__f2fs_ioctl']==0x595350
start=data.index(b'IKCFG_ST')+8;end=data.index(b'IKCFG_ED',start)
config=gzip.decompress(data[start:end]).decode()
assert all(value in config.splitlines() for value in ['CONFIG_F2FS_FS=y','CONFIG_FS_ENCRYPTION=y',
    'CONFIG_FS_ENCRYPTION_INLINE_CRYPT=y','CONFIG_DM_DEFAULT_KEY=y','CONFIG_KALLSYMS_BASE_RELATIVE=y'])
(OUT/'stock.config').write_text(config,encoding='utf-8')

# Exact command construction, conditional branch, and call from the reviewed
# stock binary. These are byte assertions, not a search for an isolated string.
assert data[0x595738:0x595748].hex()=='08a29e520801a8723f00086ba12e0054'
assert data[0x59578c:0x595790].hex()=='48d40094'
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
call=next(md.disasm(data[0x59578c:0x595790],0x59578c))
assert call.mnemonic=='bl' and int(call.op_str[1:],16)==symbols['f2fs_resize_fs']

def execute(old,requested,expected):
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    u.mem_map(0,(len(data)+4095)&~4095);u.mem_write(0,data)
    stack=0x10000000;objects=0x20000000;stop=0x30000000
    u.mem_map(stack,0x10000);u.mem_map(objects,0x10000);u.mem_map(stop,0x1000)
    filp=objects;inode=objects+0x1000;sb=objects+0x2000;sbi=objects+0x3000;raw=objects+0x5000;task=objects+0x6000
    def put(at,value,size=8):u.mem_write(at,value.to_bytes(size,'little'))
    put(filp+0xb8,inode);put(inode+0x28,sb);put(sb+0x3c8,sbi)
    put(sbi+0x10,raw);put(raw+0x24,old);put(task+0x620,0x7a96cde442105388)
    put(sbi+0xdac,1,4);put(sbi+0x6ec,9,4);put(sbi+0x6f8,1,4)
    u.reg_write(UC_ARM64_REG_PSTATE,5) # EL1h model; SP_EL0 is the task pointer.
    u.reg_write(UC_ARM64_REG_SP,stack+0x8000);u.reg_write(UC_ARM64_REG_SP_EL0,task)
    u.reg_write(UC_ARM64_REG_X0,filp);u.reg_write(UC_ARM64_REG_X1,requested);u.reg_write(UC_ARM64_REG_X30,stop)
    before=bytes(u.mem_read(objects,0x10000));trace=[];writes=[]
    def code(_,address,size,user):
        trace.append(address)
        assert symbols['f2fs_resize_fs']<=address<symbols['f2fs_resize_fs']+1244
        instruction=next(md.disasm(bytes(u.mem_read(address,size)),address))
        assert instruction.mnemonic not in ('bl','blr'),'unexpected call outside early return path'
    def write(_,access,address,size,value,user):
        assert stack<=address and address+size<=stack+0x10000,'resize prefix wrote outside its own stack'
        writes.append((address,size))
    u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,write)
    u.emu_start(symbols['f2fs_resize_fs'],stop,count=1000)
    value=u.reg_read(UC_ARM64_REG_W0)
    if value&0x80000000:value-=1<<32
    assert u.reg_read(UC_ARM64_REG_PC)==stop and value==expected
    assert bytes(u.mem_read(objects,0x10000))==before and u.reg_read(UC_ARM64_REG_SP)==stack+0x8000
    return dict(old_blocks=old,requested_blocks=requested,result=value,instructions=len(trace),
                helper_calls=0,only_stack_writes=True,objects_unchanged=True)

cases=[execute(old,1<<40,-22) for old in [1,242865747,(1<<40)-1]]
cases += [execute(242865664,242865664,0),execute(242865747,175756631,-22)]
references={}
for source in (ROOT/'research/stock-linux-350f378eb496').glob('*'):
    if source.is_file() and source.suffix!='.json':references[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
report=dict(kernel_sha256=sha,kernel_release=re.search(rb'Linux version ([^ ]+)',data)[1].decode(),
            source_signed_boot_sha256=boot.sha256,source_boot_index=boot.index,source_boot_location=boot.location,
            decoded_symbol_records=len(symbol_rows),decoded_unique_names=len(symbols),
            config_sha256=hashlib.sha256(config.encode()).hexdigest(),
            ioctl=hex(0x4008f510),ioctl_compare_offset=hex(0x595738),resize_call_offset=hex(0x59578c),
            resize_function_offset=hex(symbols['f2fs_resize_fs']),source_revision='350f378eb496',
            source_file_sha256=references,cases=cases,guest_code_patched=False,
            modeled_inputs='file/inode/superblock/task objects and EL1 registers only; no kernel helper stubs',
            limitations=['not an actual mounted F2FS or ioctl syscall execution','no valid shrink executed',
                         'no live dm geometry, backup or fscrypt migration proof'],hardware_tested=False)
(OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('source_file_sha256',)},indent=2))
