"""Execute fixed original NT36536 metadata/checksum instruction fragments.

Unicorn emulates only those arithmetic/memory instructions with synthetic RAM,
not Android, a device, Linux callbacks or QEMU. Original module stays external.
"""
from pathlib import Path
import argparse,hashlib,json,random,struct
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_SP,UC_ARM64_REG_X8,UC_ARM64_REG_X26,UC_ARM64_REG_NZCV
PIN='a3c9fce96b453bd18ab4b44fec67db4504c65b1d5f8fc6218a19f2089be9f2a4'
def section(blob,wanted):
    if blob[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',blob,18)[0]!=183:raise ValueError('Expected little-endian AArch64 ELF')
    start=struct.unpack_from('<Q',blob,40)[0];size,count,names=struct.unpack_from('<HHH',blob,58)
    if size!=64 or start+size*count>len(blob):raise ValueError('Invalid ELF section table')
    headers=[struct.unpack_from('<IIQQQQIIQQ',blob,start+i*size) for i in range(count)]
    h=headers[names];strings=blob[h[4]:h[4]+h[5]]
    for h in headers:
        name=strings[h[0]:].split(b'\0')[0].decode()
        if name==wanted:
            if h[4]+h[5]>len(blob):raise ValueError('Truncated section')
            return blob[h[4]:h[4]+h[5]]
    raise ValueError('Missing section '+wanted)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--module',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.module.read_bytes()
    if hashlib.sha256(data).hexdigest()!=PIN:raise ValueError('Wrong original module')
    code=section(data,'.text');strings=section(data,'.rodata.str1.1')
    if b'pen_id' not in strings[0x19bd:0x1a3d]:raise ValueError('F1 metadata log anchor differs')
    machine=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x100000;global_ts=0x200000;state=0x210000;stack=0x220000
    machine.mem_map(base,(len(code)+4095)&~4095);machine.mem_write(base,code)
    for addr in (global_ts,state,stack):machine.mem_map(addr,0x4000)
    machine.mem_write(global_ts,struct.pack('<Q',state))
    rng=random.Random(0x20261117);inputs=[bytes(9),b'\xff'*9,bytes(range(9))]
    inputs += [bytes(1<<(bit%8) if i==bit//8 else 0 for i in range(9)) for bit in range(72)]
    inputs += [rng.randbytes(9) for _ in range(64)]
    fixtures=[]
    for value in inputs:
        machine.mem_write(state,bytes(0x400));machine.mem_write(stack,bytes(0x200));machine.mem_write(stack+0x83,value)
        machine.reg_write(UC_ARM64_REG_SP,stack);machine.reg_write(UC_ARM64_REG_X26,global_ts);machine.reg_write(UC_ARM64_REG_X8,state)
        # Stops before _printk; relocations used only for unused log arguments.
        machine.emu_start(base+0x48f0,base+0x4a00,count=512)
        raw=bytes(machine.mem_read(state+0x22a,9));serial=struct.unpack('<I',machine.mem_read(state+0x234,4))[0]
        vendor,product=struct.unpack('<HH',machine.mem_read(state+0x238,4))
        if raw!=value:raise ValueError('Original raw metadata copy differs')
        fixtures.append({'raw':list(value),'serial':serial,'vendor':vendor,'product':product})
    envelope=[]
    for valid in (True,False):
        for _ in range(16):
            packet=bytearray(rng.randbytes(109));packet[65]=(-sum(packet[1:65])+(0 if valid else 1))&255
            machine.mem_write(stack+0x40,bytes(packet));machine.reg_write(UC_ARM64_REG_SP,stack)
            machine.emu_start(base+0x4038,base+0x4064,count=512)
            accepted=bool(machine.reg_read(UC_ARM64_REG_NZCV)&(1<<30))
            if accepted!=valid:raise ValueError('Original checksum comparison differs')
            envelope.append({'packet':list(packet),'accepted':accepted})
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'fixtures.json').write_text(json.dumps({'metadata':fixtures,'envelope':envelope},indent=2)+'\n')
    lines=['/* Synthetic fixtures produced by original-module AArch64 fragments. */','#include <stdint.h>',
        'static const struct {uint8_t raw[9];uint32_t serial;uint16_t vendor,product;} oem_info[] = {']
    lines+=['  {{'+','.join(map(str,f['raw']))+'},'+str(f['serial'])+'u,'+str(f['vendor'])+','+str(f['product'])+'},' for f in fixtures]
    lines+=['};','static const struct {uint8_t packet[109];int accepted;} oem_envelope[] = {']
    lines+=['  {{'+','.join(map(str,f['packet']))+'},'+str(int(f['accepted']))+'},' for f in envelope];lines+=['};']
    (out/'OemFixtures.h').write_text('\n'.join(lines)+'\n')
    result={'passed':True,'original_module_sha256':PIN,'instruction_fragments':{
        'f1_metadata_48f0_4a00':hashlib.sha256(code[0x48f0:0x4a00]).hexdigest(),
        'envelope_4038_4064':hashlib.sha256(code[0x4038:0x4064]).hexdigest()},
        'metadata_cases':len(fixtures),'envelope_cases':len(envelope),'fixture_header_sha256':hashlib.sha256((out/'OemFixtures.h').read_bytes()).hexdigest(),
        'emulation_scope':'instruction fragments only; synthetic memory, no kernel/device callbacks',
        'f1_is_metadata':True,'f1_checksum_validated':False,'pen_payload_checksum_verified':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
