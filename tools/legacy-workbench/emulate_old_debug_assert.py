"""Execute the accepted FD's DebugAssert; intercept only its output sink.

This verifies the assert function's return behavior, not ExitBootServices
or the real hardware output sink. Firmware code is never patched.
"""
from pathlib import Path
import hashlib
import json
import struct
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn import arm64_const as r

root=Path(__file__).resolve().parents[1]
work=root/'edl_work/ebs-20261106'
pe=(work/'MuVarPolicyFoundationDxe-old.efi').read_bytes()
assert hashlib.sha256(pe).hexdigest()=='06b6d0948c755f3faec20a975ea2fc94362d48509b009fdbbeb820f755ed8bbc'
nt=struct.unpack_from('<I',pe,60)[0]
count=struct.unpack_from('<H',pe,nt+6)[0]
optional=struct.unpack_from('<H',pe,nt+20)[0]
base=0x40000000
uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
uc.mem_map(base,0x10000)
for i in range(count):
    at=nt+24+optional+i*40
    virtual_size,rva,raw_size,raw=struct.unpack_from('<4I',pe,at+8)
    uc.mem_write(base+rva,pe[raw:raw+raw_size])
uc.mem_map(0x60000000,0x2000)
uc.mem_write(0x60000000,b'MuVarPolicyFoundationDxe.c\0')
uc.mem_write(0x60001000,b'ASSERT_EFI_ERROR (Status = Unsupported)\0')
uc.mem_map(0x70000000,0x20000)
uc.reg_write(r.UC_ARM64_REG_SP,0x7001f000)
uc.reg_write(r.UC_ARM64_REG_X0,0x60000000)
uc.reg_write(r.UC_ARM64_REG_X1,209)
uc.reg_write(r.UC_ARM64_REG_X2,0x60001000)
uc.reg_write(r.UC_ARM64_REG_X30,0x80000000)
outputs=[]
instructions=[0]

def hook(machine,address,size,user):
    instructions[0]+=1
    if address==base+0x1000:
        raise AssertionError('DebugAssert reached CpuBreakpoint')
    if address==base+0x1864:
        pointer=machine.reg_read(r.UC_ARM64_REG_X0)
        length=machine.reg_read(r.UC_ARM64_REG_X1)
        assert 0<length<=256
        outputs.append(bytes(machine.mem_read(pointer,length)).decode('ascii'))
        machine.reg_write(r.UC_ARM64_REG_X0,length)
        machine.reg_write(r.UC_ARM64_REG_PC,machine.reg_read(r.UC_ARM64_REG_X30))

uc.hook_add(UC_HOOK_CODE,hook)
uc.emu_start(base+0x15f8,0x80000000,count=200000)
assert uc.reg_read(r.UC_ARM64_REG_PC)==0x80000000
assert len(outputs)==1 and 'ASSERT [MuVarPolicyFoundationDxe]' in outputs[0]
assert 'MuVarPolicyFoundationDxe.c(209)' in outputs[0]
report=dict(pe_sha256=hashlib.sha256(pe).hexdigest(),function_rva='0x15f8',
    output_sink_rva='0x1864',output=outputs[0],instructions=instructions[0],
    returned_to_caller=True,code_patched=False,breakpoint_or_deadloop_reached=False,
    output_sink_intercepted=True,ExitBootServices_tested=False,hardware_tested=False)
(work/'debug-assert-execution.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
