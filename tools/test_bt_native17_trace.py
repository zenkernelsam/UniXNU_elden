"""Execute diagnostic stubs and verify native register, stack and syscall ABI."""
from pathlib import Path
import argparse,json,struct,sys
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_INTR
from unicorn.arm64_const import *
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'drivers/EldenBluetooth/userland'))
from Native17 import Image,BASE
from Native17Trace import apply,MARKER,MAGIC
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    image=apply(Image(a.image).routing());reports=[]
    for slide in (0,0x28000000):
        for kind in ('xpc_result_trace','errx_trace'):
            for value in (0,1,0x123456789abcdef):
                u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(BASE+slide,0xa00000)
                for name,va,vs,off,size,prot in image.segments:
                    if name!='__PAGEZERO':u.mem_write(va+slide,bytes(image.data[off:off+size]))
                u.mem_map(0x300000000,0x10000);sp=0x300008000;end=0x300009000
                u.reg_write(UC_ARM64_REG_SP,sp);u.reg_write(UC_ARM64_REG_LR,end)
                u.reg_write(UC_ARM64_REG_X0,value);u.reg_write(UC_ARM64_REG_X1,0xaaaabbbbcccc)
                u.reg_write(UC_ARM64_REG_X20,0x111122223333)
                u.reg_write(UC_ARM64_REG_X29,0xdeadbeef)
                calls=[];stops=[]
                def interrupt(emu,num,context):
                    assert num==2
                    args=[emu.reg_read(r) for r in (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_X4)]
                    assert emu.reg_read(UC_ARM64_REG_X16)==179
                    assert args==[MARKER,1 if kind=='xpc_result_trace' else 2,value,0x111122223333 if kind=='xpc_result_trace' else 0xaaaabbbbcccc,MAGIC]
                    calls.append(args);emu.reg_write(UC_ARM64_REG_X0,5);emu.reg_write(UC_ARM64_REG_X1,9)
                def hook(emu,pc,size,context):
                    if pc==end or pc==BASE+slide+0x708fbc:
                        stops.append(pc);emu.emu_stop()
                u.hook_add(UC_HOOK_INTR,interrupt);u.hook_add(UC_HOOK_CODE,hook)
                u.emu_start(image.entries[kind]+slide,0,count=100)
                assert len(calls)==1 and len(stops)==1
                assert u.reg_read(UC_ARM64_REG_X0)==value and u.reg_read(UC_ARM64_REG_SP)==sp and u.reg_read(UC_ARM64_REG_LR)==end
                assert u.reg_read(UC_ARM64_REG_X29)==0xdeadbeef and u.reg_read(UC_ARM64_REG_X20)==0x111122223333
                if kind=='errx_trace':assert u.reg_read(UC_ARM64_REG_X1)==0xaaaabbbbcccc
                else:assert u.reg_read(UC_ARM64_REG_X19)==value
                reports.append({'slide':hex(slide),'kind':kind,'native_result':value,'syscall':calls[0]})
    a.output.mkdir(parents=True);(a.output/'result.json').write_text(json.dumps({'cases':reports,'hardware':False,'libc_errno_untouched':True,'patch':image.report()},indent=2)+'\n')
    print('PASS',len(reports),'trace/ABI cases, including syscall error result')
if __name__=='__main__':main()
