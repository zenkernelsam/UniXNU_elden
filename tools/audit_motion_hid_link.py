"""Audit linked original OS17 motion device inheritance and event call bindings."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    raw=(a.kernel_build/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes();spec=json.loads((a.kernel_build/'result.json').read_text())
    if sha(a.kernel_build/'kernel.macho')!=spec['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=spec['native_sha256']:raise ValueError('Kernel identity changed')
    im=macho(raw);ni=macho(native);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(name):
        v=local.get(name,set()).copy()
        if name in names:v.add(names[name])
        if len(v)!=1:raise ValueError('Unique symbol required: '+name)
        return v.pop()
    def read(addr,n):
        off=backed_offset(im['segments'],addr,n,len(raw));return raw[off:off+n]
    parent=struct.unpack('<207Q',read(0xfffffff005d045c0,207*8));child=struct.unpack('<207Q',read(symbol('__ZTV14EldenMotionHid')+16,207*8))
    expected={0:'__ZN14EldenMotionHidD1Ev',1:'__ZN14EldenMotionHidD0Ev',7:'__ZNK14EldenMotionHid12getMetaClassEv',
        14:'__ZN14EldenMotionHid4freeEv',86:'__ZN14EldenMotionHid5startEP9IOService',87:'__ZN14EldenMotionHid4stopEP9IOService',
        111:'__ZNK14EldenMotionHid11getWorkLoopEv',171:'__ZNK14EldenMotionHid18newTransportStringEv',
        173:'__ZNK14EldenMotionHid16newProductStringEv',180:'__ZNK14EldenMotionHid19newReportDescriptorEPP18IOMemoryDescriptor'}
    delta=[i for i in range(207) if parent[i]!=child[i]]
    if delta!=sorted(expected):raise ValueError('Motion IOHIDDevice override extent differs')
    for slot,n in expected.items():
        addr=symbol(n)
        if child[slot]!=addr:raise ValueError('Wrong motion override slot')
        off=backed_offset(ni['segments'],addr,32,len(native))
        if native[off:off+32]!=read(addr,32):raise ValueError('Actual motion method changed in prelink')
    if struct.unpack('<Q',read(symbol('__ZN14EldenMotionHid10superClassE'),8))[0]!=symbol('__ZN11IOHIDDevice10gMetaClassE'):
        raise ValueError('Wrong original HID superclass')
    init=[]
    for seg in im['segments']:
        for sec in seg['sections']:
            if sec['name']=='__mod_init_func':
                b=raw[sec['fileoff']:sec['fileoff']+sec['size']];init.extend(struct.unpack('<%dQ'%(len(b)//8),b))
    if symbol('__GLOBAL__sub_I_EldenMotionHid.cpp') in init:raise ValueError('Automatic motion initializer')
    signatures=json.loads((Path(__file__).resolve().parents[1]/'drivers/EldenMotion/hid-source.json').read_text())
    for n,row in signatures['bound_original_OS17'].items():
        if hashlib.sha256(read(int(row['va'],16),row['bytes'])).hexdigest()!=row['sha256']:raise ValueError('Original method signature differs')
    # Exact original constructor/dispatch addresses are materialized and invoked
    # through registers; they are not reachable by an out-of-range direct BL.
    dis=subprocess.check_output(['xcrun','llvm-objdump','--disassemble','--no-show-raw-insn',a.kernel_build/'native.macho'],text=True)
    name='__ZN14EldenMotionHid8dispatchEP8OSObjectPvS2_S2_S2_';at=dis.index('<'+name+'>:');end=dis.find('\n\n',at);body=dis[at:end]
    required=[0xfffffff004afb970,0xfffffff004afb9c8,0xfffffff004aface8]
    instructions=[]
    for line in body.splitlines():
        m=re.match(r'^\s*([0-9a-fA-F]+):\s+([a-z][a-z0-9.]*)\s*(.*)$',line)
        if m:instructions.append((int(m[1],16),m[2],m[3].split(';')[0].strip()))
    registers={};indirect=[]
    for pc,op,args in instructions:
        matched=re.fullmatch(r'([xw]\d+), #(-?0x[0-9a-f]+|-?\d+)',args)
        if op=='mov' and matched:
            rd,imm=matched.groups();bits=64 if rd.startswith('x') else 32;registers['x'+rd[1:]]={int(imm,0)&((1<<bits)-1)};continue
        matched=re.fullmatch(r'([xw]\d+), #(-?0x[0-9a-f]+|-?\d+)(?:, lsl #(0x[0-9a-f]+|\d+))?',args)
        if op=='movk' and matched:
            rd,imm,shift=matched.groups();shift=int(shift or '0',0);key='x'+rd[1:]
            if key in registers:registers[key]={(v&~(0xffff<<shift))|((int(imm,0)&0xffff)<<shift) for v in registers[key]}
            continue
        matched=re.fullmatch(r'(x\d+), (x\d+), #(0x[0-9a-f]+|\d+)',args)
        if op=='add' and matched:
            rd,rn,imm=matched.groups()
            if rn in registers:registers[rd]={(v+int(imm,0))&((1<<64)-1) for v in registers[rn]}
            else:registers.pop(rd,None)
            continue
        matched=re.fullmatch(r'(x\d+), (x\d+), (x\d+), \w+',args)
        if op=='csel' and matched:
            rd,a0,a1=matched.groups()
            if a0 in registers and a1 in registers:registers[rd]=registers[a0]|registers[a1]
            else:registers.pop(rd,None)
            continue
        if op=='blr':
            values=registers.get(args)
            if values:indirect.append({'pc':hex(pc),'targets':sorted(hex(v) for v in values)})
            for i in range(19):registers.pop('x'+str(i),None)
            continue
        if op=='bl':
            for i in range(19):registers.pop('x'+str(i),None)
            continue
        # Only constants are tracked. Any unhandled write invalidates its
        # destination, while comparisons/stores/branches preserve registers.
        if op not in ('cmp','cmn','tst','str','stur','stp','stlr','stlrb','stlrh','b','ret','cbz','cbnz','tbz','tbnz') and not op.startswith('b.'):
            rd=re.match(r'([xw]\d+)',args)
            if rd:registers.pop('x'+rd[1][1:],None)
            if op=='ldp':
                pairs=re.match(r'([xw]\d+), ([xw]\d+)',args)
                if pairs:registers.pop('x'+pairs[2][1:],None)
    expected_pair={hex(required[0]),hex(required[1])}
    if not any(set(r['targets'])==expected_pair for r in indirect) or not any(r['targets']==[hex(required[2])] for r in indirect):
        raise ValueError('Actual constant constructor/dispatch indirect targets differ')
    for name in ['_elden_motion_native_hid_start','_elden_motion_native_hid_bind','_elden_motion_native_hid_packet','_elden_motion_native_hid_stop']:
        symbol(name)
    result={'motion_component_original_HID_ABI_passed':True,'kernel_sha256':spec['kernel_sha256'],'native_sha256':spec['native_sha256'],
        'device_slots':207,'overrides':{str(i):n for i,n in expected.items()},'constructor_signature_match':True,
        'actual_dispatch_disassembly_sha256':hashlib.sha256(body.encode()).hexdigest(),'original_event_targets':[hex(n) for n in required],
        'actual_materialized_indirect_targets':indirect,
        'late_initializer_not_automatic':True,'actual_Gen4_producer_connected':False,'device_orientation_service':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: motion IOHIDDevice207slots/10overrides, original constructors/driver and late initialization')
if __name__=='__main__':main()
