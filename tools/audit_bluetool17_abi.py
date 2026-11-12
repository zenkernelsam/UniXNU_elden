"""Verify the original 21H461 client's BlueTool XPC ABI without patching it."""
import argparse,hashlib,json,struct
from pathlib import Path
EXPECTED='3f1e4493ea6a76976625f7af58a8f330c0c90d82d9020b11110b4f3f5b758df4'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bluetoothd',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    b=a.bluetoothd.read_bytes()
    if hashlib.sha256(b).hexdigest()!=EXPECTED:raise ValueError('Expected original 21H461 bluetoothd')
    if struct.unpack_from('<4I',b)!=(0xfeedfacf,0x100000c,0,2):raise ValueError('Expected ordinary arm64 executable')
    segments=[];sections=[];sym=ind=chain=None;at=32;uuid=None
    for _ in range(struct.unpack_from('<I',b,16)[0]):
        cmd,size=struct.unpack_from('<II',b,at)
        if cmd==0x19:
            va,n,off,filesize=struct.unpack_from('<4Q',b,at+24);segments.append((va,n,off,filesize))
            for i in range(struct.unpack_from('<I',b,at+64)[0]):
                q=at+72+80*i;sva,sn,soff=struct.unpack_from('<QQI',b,q+32);flags,r1,r2=struct.unpack_from('<III',b,q+64)
                sections.append((sva,sn,soff,flags,r1,r2))
        if cmd==2:sym=struct.unpack_from('<4I',b,at+8)
        if cmd==0xb:ind=struct.unpack_from('<II',b,at+56)
        if cmd==0x80000034:chain=struct.unpack_from('<II',b,at+8)
        if cmd==0x1b:uuid=b[at+8:at+24].hex()
        at+=size
    def read(va,n):
        for base,_,off,size in segments:
            if base<=va and va+n<=base+size:return b[off+va-base:off+va-base+n]
        raise ValueError('VA outside file-backed ranges')
    def word(va):return struct.unpack('<I',read(va,4))[0]
    def string(va):return read(va,128).split(b'\0')[0].decode()
    names=[]
    for i in range(sym[1]):
        pos=sym[2]+struct.unpack_from('<I',b,sym[0]+16*i)[0];names.append(b[pos:b.index(b'\0',pos)].decode())
    stubs={}
    for va,n,off,flags,r1,r2 in sections:
        if flags&255!=8:continue
        for i in range(n//r2):
            ix=struct.unpack_from('<I',b,ind[0]+4*(r1+i))[0]
            if ix<len(names):stubs[va+r2*i]=names[ix]
    def call(va,expected):
        value=word(va)
        if value&0xfc000000!=0x94000000:raise ValueError('Expected BL')
        imm=value&0x3ffffff
        if imm&(1<<25):imm-=1<<26
        if stubs.get(va+(imm<<2))!=expected:raise ValueError('Import call changed')
    def literal(va,register,expected):
        adrp,add=word(va),word(va+4)
        if adrp&0x9f000000!=0x90000000 or add&0xff000000!=0x91000000:
            raise ValueError('Expected ADRP/ADD literal load')
        if adrp&31!=register or add&31!=register or (add>>5)&31!=register:raise ValueError('Argument register changed')
        imm=((adrp>>5)&0x7ffff)<<2|((adrp>>29)&3)
        if imm&(1<<20):imm-=1<<21
        address=(va&~4095)+(imm<<12)+(((add>>10)&4095)<<(12 if add&(1<<22) else 0))
        if expected is not None and string(address)!=expected:raise ValueError('Literal changed')
        return hex(address)
    literals={}
    for label,va,reg,text in [('service',0x1002ef9cc,2,'com.apple.BlueTool'),('script',0x1002ef9f0,1,'script'),
        ('command',0x1002efabc,1,'RunBuiltinScript'),('result',0x1002efdb4,1,'result'),
        ('firmware',0x1002efe50,1,'firmware'),('manufacturer',0x1002efe64,1,'manufacturer'),
        ('address',0x1002efa28,1,'btaddress'),('address_format',0x10042764c,2,'%02X:%02X:%02X:%02X:%02X:%02X')]:
        literals[label]={'address':literal(va,reg,text),'value':text}
    for va,name in [(0x1002ef9fc,'_xpc_dictionary_set_string'),(0x1002efdc0,'_xpc_dictionary_get_int64'),
                    (0x1002efe5c,'_xpc_dictionary_get_string'),(0x1002efe70,'_xpc_dictionary_get_int64')]:call(va,name)
    starts=chain[0]+struct.unpack_from('<I',b,chain[0]+4)[0]
    array=0x1008d6fa8;seg=next(i for i,(va,size,_,_) in enumerate(segments) if va<=array<va+size)
    if int(literal(0x1002514d4,8,None),16)!=array:raise ValueError('Envelope key-array reference changed')
    call(0x100251500,'_xpc_dictionary_create')
    segment_start=starts+struct.unpack_from('<I',b,starts+4+4*seg)[0]
    pointer_format=struct.unpack_from('<H',b,segment_start+6)[0]
    if pointer_format!=6:raise ValueError('Expected DYLD_CHAINED_PTR_64_OFFSET')
    keys=[]
    for value in struct.unpack('<2Q',read(array,16)):
        if value>>63 or (value>>36)&0x7fff:raise ValueError('Expected low-address non-bind 64-bit rebase')
        address=0x100000000+(value&((1<<36)-1))
        keys.append(string(address))
    if keys!=['kMsgId','kMsgArgs']:raise ValueError('Message envelope keys changed')
    if read(0x1002ef860,8).hex()!='ff8304d1e9230c6d' or read(0x1000ddd20,4).hex()!='a0050034':raise ValueError('Original F2/F3 differ')
    result={'file_sha256':EXPECTED,'uuid':uuid,'architecture':'arm64','literals':literals,'envelope_keys':keys,'pointer_format':pointer_format,
            'result_type':'int64','manufacturer_type':'int64','firmware_type':'string','F2_unchanged':True,'F3_unchanged':True,
            'hardware_tested':False,'bluetoothd_modified':False,'limits':['Message ABI match does not prove chipset routing, Skywalk/stream transport or HCI command compatibility.']}
    if a.output.exists():raise ValueError('Output must be new')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'uuid':uuid,'envelope_keys':keys,'pointer_format':pointer_format,'bluetoothd_modified':False}))
if __name__=='__main__':main()
