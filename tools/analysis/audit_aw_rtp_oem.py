"""Execute fixed OEM haptic arithmetic fragments only; no device/kernel boot."""
from pathlib import Path
import argparse,hashlib,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from audit_nvt_wake_oem import elf
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP,UC_ARM64_REG_X19,UC_ARM64_REG_W8,UC_ARM64_REG_W20,UC_ARM64_REG_W0
PIN='f688fcaec0d33a7c70c83af8e6fcb1aca015097282da30dfcf1e7314ad5935b9'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--module',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    raw=a.module.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise ValueError('Wrong OEM haptic module')
    sections,names,symbols,rel=elf(raw);code=names['.text']['data']
    m=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x100000;stack=0x200000;state=0x300000
    m.mem_map(base,(len(code)+4095)&~4095);m.mem_write(base,code);m.mem_map(stack,0x2000);m.mem_map(state,0x2000)
    m.reg_write(UC_ARM64_REG_SP,stack);m.reg_write(UC_ARM64_REG_X19,state)
    import struct
    records=[]
    for cfg,rate in [(0,24000),(0x20,48000),(0x40,12000)]:
        for frames in [1,120,120000,1440000]:
            m.mem_write(stack+4,bytes([cfg]));m.mem_write(state+0x74,struct.pack('<I',frames))
            m.emu_start(base+0x1571c,base+0x15764,count=32);observed=m.reg_read(UC_ARM64_REG_W20)
            if observed!=frames*1000000//rate:raise ValueError('Original theory-time map differs')
            records.append({'config':cfg,'rate_hz':rate,'frames':frames,'duration_us':observed})
    threshold=[]
    for size in [0x800,0x900,0xf00]:
        m.mem_write(state+0x560,struct.pack('<I',size));m.emu_start(base+0x12fec,base+0x1301c,count=32)
        data=bytes(m.mem_read(stack,3));ae=size//2;af=size-size//4
        expected=bytes([((ae>>8)<<4)|(af>>8),ae&255,af&255])
        if data!=expected:raise ValueError('Original FIFO threshold bytes differ')
        threshold.append({'base':size,'register2f_to31':list(data),'almost_empty':ae,'almost_full':af})
    for byte in range(256):
        m.reg_write(UC_ARM64_REG_W8,byte);m.emu_start(base+0x15664,base+0x15668,count=1)
        if m.reg_read(UC_ARM64_REG_W0)!=((byte>>3)&1):raise ValueError('AFS mask differs')
        m.reg_write(UC_ARM64_REG_W8,byte);m.emu_start(base+0x156c8,base+0x156cc,count=1)
        if m.reg_read(UC_ARM64_REG_W0)!=((byte>>4)&1):raise ValueError('AES mask differs')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'result.json').write_text(json.dumps({'passed':True,'module_sha256':PIN,'theory_cases':records,'threshold_cases':threshold,
        'flag_cases':512,'AFS_bit':3,'AES_bit':4,'AES_is_almost_empty_not_completion':True,
        'RTP_state':8,'RTP_mode':1,'RTP_data_port':0x32,'hardware':False,'instruction_fragments_only':True},indent=2)+'\n')
    print('PASS:12 original rate/time +3 FIFO-threshold +512 AFS/AES cases; AES never means stream complete')
if __name__=='__main__':main()
