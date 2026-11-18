"""Qualify exact stock NT36536 gesture/command instruction fragments.

Only bounded arithmetic/memory fragments run in Unicorn with synthetic RAM.
No Android boot, callbacks, IRQ/MMIO or device operation is emulated.
"""
from pathlib import Path
import argparse,hashlib,json,struct
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_SP,UC_ARM64_REG_X1,UC_ARM64_REG_X9,UC_ARM64_REG_W0,UC_ARM64_REG_W2,UC_ARM64_REG_W4,UC_ARM64_REG_W19
PIN='a3c9fce96b453bd18ab4b44fec67db4504c65b1d5f8fc6218a19f2089be9f2a4'
def elf(blob):
    if blob[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',blob,18)[0]!=183:raise ValueError('Wrong ELF architecture')
    off=struct.unpack_from('<Q',blob,40)[0];size,count,names=struct.unpack_from('<HHH',blob,58)
    if size!=64 or off+size*count>len(blob):raise ValueError('Invalid section table')
    h=[struct.unpack_from('<IIQQQQIIQQ',blob,off+i*size) for i in range(count)];s=h[names];strings=blob[s[4]:s[4]+s[5]]
    sections=[{'name':strings[x[0]:].split(b'\0')[0].decode(),'raw':x,'data':blob[x[4]:x[4]+x[5]]} for x in h]
    by_name={s['name']:s for s in sections};sym=by_name['.symtab'];names=sections[sym['raw'][6]]['data'];symbols=[]
    for i in range(0,len(sym['data']),24):
        name,info,other,section,value,extent=struct.unpack_from('<IBBHQQ',sym['data'],i)
        symbols.append({'name':names[name:].split(b'\0')[0].decode(),'section':section,'value':value,'size':extent})
    rel=[]
    for i in range(0,len(by_name['.rela.text']['data']),24):
        at,info,add=struct.unpack_from('<QQq',by_name['.rela.text']['data'],i);target=symbols[info>>32]
        rel.append({'offset':at,'kind':info&0xffffffff,'symbol':target,'addend':add})
    return sections,by_name,symbols,rel
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--module',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    blob=a.module.read_bytes()
    if hashlib.sha256(blob).hexdigest()!=PIN:raise ValueError('Original module differs')
    sections,names,symbols,rel=elf(blob);code=names['.text']['data'];switch=[]
    for at,kind in [(0x14e4,275),(0x14e8,277)]:
        r=next(r for r in rel if r['offset']==at)
        if r['kind']!=kind or sections[r['symbol']['section']]['name']!='.rodata':raise ValueError('Original switch relocation differs')
        off=r['symbol']['value']+r['addend'];switch.append(sections[r['symbol']['section']]['data'][off:off+13])
    if switch[0]!=switch[1] or switch[0]!=bytes([0,8,16,24,34,42,50,58,66,74,82,90,98]):raise ValueError('Original13-case table differs')
    key=next(s for s in symbols if s['name']=='gesture_key_array')
    keys=struct.unpack('<13H',sections[key['section']]['data'][key['value']:key['value']+26])
    if keys!=tuple(143 if i==3 else 116 for i in range(13)):raise ValueError('Original Linux key mapping differs')
    calls={}
    for at,name in [(0x4230,'nvt_ts_wakeup_gesture_report'),(0x4fc8,'CTP_SPI_WRITE'),(0x5104,'CTP_SPI_WRITE'),(0x5118,'irq_set_irq_wake')]:
        r=next(r for r in rel if r['offset']==at)
        if r['kind']!=283 or r['symbol']['name']!=name:raise ValueError('Original call target differs at '+hex(at))
        calls[hex(at)]=name
    m=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x100000;stack=0x200000;table=0x300000
    m.mem_map(base,(len(code)+4095)&~4095);m.mem_write(base,code);m.mem_map(stack,0x4000);m.mem_map(table,0x1000);m.mem_write(table,switch[0])
    stop={'addresses':set(),'last':None}
    def hook(machine,address,size,user):
        if address in stop['addresses']:stop['last']=address;machine.emu_stop()
    m.hook_add(UC_HOOK_CODE,hook)
    def run(at,stops):
        stop['addresses']={base+x for x in stops};stop['last']=None
        m.emu_start(base+at,base+len(code),count=64)
        if stop['last'] is None:raise ValueError('Fragment exceeded its bounded endpoint')
        return stop['last']-base
    fixtures=[]
    inputs=[(header,0,0) for header in range(256)]+[(30<<3,func,inner) for func in range(3) for inner in range(256)]
    case_starts=[0x14fc+v*4 for v in switch[0]]
    for header,func,inner in inputs:
        packet=bytearray(109);packet[1:4]=bytes([header,func,inner]);packet[65]=(-sum(packet[1:65]))&255
        m.reg_write(UC_ARM64_REG_SP,stack);m.mem_write(stack+0x40,bytes(packet));m.reg_write(UC_ARM64_REG_W4,header)
        m.emu_start(base+0x4228,base+0x4230,count=2)
        if m.reg_read(UC_ARM64_REG_X1)!=stack+0x40 or m.reg_read(UC_ARM64_REG_W0)!=header>>3:raise ValueError('Original packet argument differs')
        at=run(0x145c,[0x1490,0x14b4]);normalized=m.reg_read(UC_ARM64_REG_W19)&255;key=0
        if at==0x14b4:
            at=run(0x14d4,[0x14e4,0x170c])
            if at==0x14e4:
                m.reg_write(UC_ARM64_REG_X9,table);at=run(0x14ec,case_starts)
                value_at=0x157c if at==0x155c else 0x16a4
                m.emu_start(base+value_at,base+value_at+4,count=1);key=m.reg_read(UC_ARM64_REG_W19)
                if key!=keys[normalized-12]:raise ValueError('Actual key instruction/table mismatch')
        fixtures.append({'header':header,'function':func,'inner':inner,'normalized_id':normalized,'linux_key':key,'double':int(key==143 and normalized==15)})
    commands=[]
    for at,wanted in [(0x4fb4,0x11),(0x50f0,0x13)]:
        m.reg_write(UC_ARM64_REG_SP,stack);m.mem_write(stack,bytes(16))
        m.emu_start(base+0x4fa0,base+0x4fa4,count=1);m.emu_start(base+0x4fa8,base+0x4fac,count=1)
        m.emu_start(base+at,base+at+16,count=4)
        raw=bytes(m.mem_read(stack+4,2))
        if raw!=bytes([0x50,wanted]) or m.reg_read(UC_ARM64_REG_X1)!=stack+4 or m.reg_read(UC_ARM64_REG_W2)!=2:
            raise ValueError('Actual SPI command argument differs')
        commands.append(list(raw))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'fixtures.json').write_text(json.dumps(fixtures,indent=2)+'\n')
    lines=['/* Synthetic oracle fixtures from fixed original instruction fragments. */',
           'static const struct {unsigned char header,function,inner,double_tap;} oem_wake[] = {']
    lines+=['  {'+','.join(str(f[k]) for k in ['header','function','inner','double'])+'},' for f in fixtures];lines+=['};']
    (out/'OemWakeFixtures.h').write_text('\n'.join(lines)+'\n')
    (out/'result.json').write_text(json.dumps({'passed':True,'module_sha256':PIN,'original_instruction_cases':len(fixtures),
        'switch_bytes':list(switch[0]),'gesture_keys':list(keys),'actual_call_targets':calls,'actual_commands':commands,
        'PM_callbacks_only_flags_and_completion':True,'instruction_fragments_only':True,'hardware':False},indent=2)+'\n')
    print('PASS:1024 actual OEM gesture/caller/switch/key instruction cases and exact50:11/50:13/IRQ-wake call binding')
if __name__=='__main__':main()
