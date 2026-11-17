"""Run the original OS17 HID accessors over synthetic event storage.

This qualifies field layout/readback, not UIKit angle units or live matching.
No kernel, device, callback or hardware is booted. Requires Unicorn/XnuPrelink.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_SP,UC_ARM64_REG_LR,UC_ARM64_REG_PC
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',type=Path,required=True);p.add_argument('--expected-sha256',required=True)
    p.add_argument('--prelink-tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    raw=a.kernel.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=a.expected_sha256:raise ValueError('Reference image mismatch')
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset
    image=macho(raw);u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);ranges=[]
    for s in image['segments']:
        if s['filesize']:ranges.append((s['va']&~4095,(s['va']+s['filesize']+4095)&~4095))
    merged=[]
    for lo,hi in sorted(ranges):
        if merged and lo<=merged[-1][1]:merged[-1]=(merged[-1][0],max(hi,merged[-1][1]))
        else:merged.append((lo,hi))
    for lo,hi in merged:u.mem_map(lo,hi-lo)
    for s in image['segments']:
        if s['filesize']:u.mem_write(s['va'],raw[s['fileoff']:s['fileoff']+s['filesize']])
    obj,data,stack,ret=0x200000,0x210000,0x220000,0x230000
    for addr in (obj,data,stack,ret):u.mem_map(addr,0x10000)
    event_vptr=0xfffffff005d07318
    at=backed_offset(image['segments'],event_vptr,37*8,len(raw));table=struct.unpack_from('<37Q',raw,at)
    methods={16:0xfffffff004afb494,23:0xfffffff004afcfb8,24:0xfffffff004afefdc,25:0xfffffff004afe4c4,26:0xfffffff004affd00,27:0xfffffff004afef64}
    if any(table[i]!=v for i,v in methods.items()):raise ValueError('Original accessor table differs')
    u.mem_write(obj,struct.pack('<QQQQ',event_vptr,0,data,0));u.mem_write(obj+48,struct.pack('<Q',0x12345678))
    def fresh(kind=11):
        body=bytearray(128);struct.pack_into('<II',body,0,128,kind);return body
    def call(slot,field=0,value=0,body=None):
        u.mem_write(data,bytes(fresh() if body is None else body))
        for reg,v in [(UC_ARM64_REG_X0,obj),(UC_ARM64_REG_X1,field),(UC_ARM64_REG_X2,value),(UC_ARM64_REG_X3,0),(UC_ARM64_REG_SP,stack+0x8000),(UC_ARM64_REG_LR,ret)]:u.reg_write(reg,v)
        u.emu_start(methods[slot],ret,count=20000)
        if u.reg_read(UC_ARM64_REG_PC)!=ret:raise ValueError('Accessor did not return')
        return u.reg_read(UC_ARM64_REG_X0),bytes(u.mem_read(data,128))
    checks=0
    def check(ok):
        nonlocal checks
        checks+=1
        if not ok:raise ValueError('Original accessor assertion '+str(checks))
    check(call(16)[0]==0x12345678)
    # Public accessor reports orientation type, but does not set it.
    check(call(24,0xb001f,1)[1]==bytes(fresh()))
    for orientation in (0,1,2):
        b=fresh();struct.pack_into('<I',b,64,orientation)
        check(call(23,0xb001f,body=b)[0]==orientation)
        for field,offset in [(0xb000f,68),(0xb0010,72)]:
            for angle in (0,45*65536,90*65536):
                _,after=call(26,field,angle,b)
                if orientation==1:
                    expected=bytearray(b);struct.pack_into('<I',expected,offset,angle)
                    check(after==bytes(expected));check(call(25,field,body=after)[0]==angle)
                else:check(after==bytes(b))
    vendor=fresh(1);struct.pack_into('<HHII',vendor,16,0xffe0,1,0,8);vendor[28:36]=bytes([0xe7,1,45,0,1,0,2,0])
    for field,value in [(0x10000,0xffe0),(0x10001,1),(0x10003,8)]:check(call(23,field,body=vendor)[0]==value)
    check(call(27,0x10004,body=vendor)[0]==data+28)
    # Exact pending-message path before original dispatch appends children.
    site=0xfffffff004afad08;at=backed_offset(image['segments'],site,8,len(raw));words=struct.unpack_from('<II',raw,at)
    check(words==(0xf9408008,0xf940b900)) # ldr x8,[x0,#0x100]; ldr x0,[x8,#0x170]
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    result={'passed':True,'checks':checks,'reference_kernel_sha256':a.expected_sha256,
        'methods':{str(k):hex(v) for k,v in methods.items()},'event_data_offset':16,'event_minimum_bytes_for_polar':76,
        'orientation_offset':64,'altitude_offset':68,'azimuth_offset':72,'orientation_field':'0xb001f','polar_type':1,
        'altitude_field':'0xb000f','azimuth_field':'0xb0010','orientation_setter_is_noop':True,
        'driver_expansion_offset':256,'pending_vendor_offset':368,'pending_load_words':[hex(x) for x in words],
        'vendor_fields':{'page':'0x10000','usage':'0x10001','length':'0x10003','data':'0x10004'},
        'scope':'Original accessors/instruction anchors with synthetic memory; no UIKit or hardware claim',
        'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
