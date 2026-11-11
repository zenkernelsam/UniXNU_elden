"""Execute the built ARM64 probe with modeled syscalls; never access a device."""
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_INTR
from unicorn.arm64_const import UC_ARM64_REG_SP,UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X8,UC_ARM64_REG_CPACR_EL1

root=Path(__file__).resolve().parents[1];out=root/'edl_work/stock-f2fs-20261106'
data=(out/'f2fs-resize-probe').read_bytes()
assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183
entry=struct.unpack_from('<Q',data,24)[0];phoff=struct.unpack_from('<Q',data,32)[0]
phsize,phcount=struct.unpack_from('<HH',data,54)
segments=[]
for i in range(phcount):
    kind,flags,offset,address,_,filesize,memsize,align=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)
    if kind==1:segments.append((offset,address,filesize,memsize))
release='6.6.118-android15-8-g350f378eb496-ab14943902-4k'
def run(name,kernel=release,magic=0xf2f52010,blocksize=4096,blocks=242865747,ioctl=-22):
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    lo=min(a for _,a,_,_ in segments)&~4095
    hi=(max(a+m for _,a,_,m in segments)+4095)&~4095
    u.mem_map(lo,hi-lo)
    for offset,address,filesize,memsize in segments:u.mem_write(address,data[offset:offset+filesize])
    stack=0x40000000;u.mem_map(stack,0x10000);sp=stack+0x8000
    u.mem_write(sp,struct.pack('<QQQQQ',2,stack+0x100,stack+0x180,0,0))
    u.mem_write(stack+0x100,b'probe\0');u.mem_write(stack+0x180,b'/data\0')
    u.reg_write(UC_ARM64_REG_SP,sp);u.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
    calls=[];text=[];exits=[];requested=[]
    def syscall(_,number,user):
        n=u.reg_read(UC_ARM64_REG_X8);x0=u.reg_read(UC_ARM64_REG_X0);x1=u.reg_read(UC_ARM64_REG_X1);x2=u.reg_read(UC_ARM64_REG_X2)
        calls.append(n);result=0
        if n==160:
            uts=bytearray(390);uts[130:130+len(kernel)]=kernel.encode();u.mem_write(x0,bytes(uts))
        elif n==43:u.mem_write(x1,struct.pack('<QQQ',magic,blocksize,blocks)+bytes(256-24))
        elif n==56:result=5
        elif n==29:
            assert x0==5 and x1==0x4008f510
            value=struct.unpack('<Q',u.mem_read(x2,8))[0];assert value==1<<40;requested.append(value);result=ioctl
        elif n==57:assert x0==5
        elif n==64:
            assert x0==1;text.append(bytes(u.mem_read(x1,x2)).decode());result=x2
        elif n==93:exits.append(x0);u.emu_stop()
        else:raise AssertionError('unexpected syscall '+str(n))
        u.reg_write(UC_ARM64_REG_X0,result&((1<<64)-1))
    u.hook_add(UC_HOOK_INTR,syscall);u.emu_start(entry,0,count=50000)
    assert len(exits)==1
    if name=='audited-growth':assert requested==[1<<40] and exits==[0]
    elif name in ('wrong-kernel','not-f2fs','wrong-blocksize','oversize-fs'):assert not requested and 56 not in calls and exits!=[0]
    else:assert requested==[1<<40] and exits!=[0]
    return dict(case=name,syscalls=calls,requested_blocks=requested,exit_code=exits[0],message=''.join(text))
cases=[run('audited-growth'),run('wrong-kernel',kernel='6.6.118-other'),run('not-f2fs',magic=0xef53),
       run('wrong-blocksize',blocksize=512),run('oversize-fs',blocks=1<<40),run('denied',ioctl=-1),
       run('readonly',ioctl=-30),run('unavailable',ioctl=-25)]
report=dict(probe_sha256=hashlib.sha256(data).hexdigest(),probe_bytes=len(data),cases=cases,
            modeled_syscalls=True,real_kernel_ioctls_executed=False,hardware_tested=False)
(out/'probe-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
