"""Restore Jerry's documented SM8750 bus origin; preserve every MMIO address.

Accept only the exact R1 ADT. Outputs an experimental ADT and evidence, no device I/O.
"""
import gzip
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_boot_candidate import Fat

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interrupt-alias',action='store_true',help='also publish the existing GIC phandle under the XNU AAPL key')
    args=parser.parse_args()
    image=(ROOT/'edl_work/rootcheck-entry-20261106/boot-signed.img').read_bytes()
    raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
    data=Fat(raw[0x400080:]).read('/EFI/UniXNU/Payloads/kernel.macho.adt')
    assert hashlib.sha256(data).hexdigest()=='b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3'
    nodes={}; metadata={}
    def walk(off,parent):
        start=off
        np,nc=struct.unpack_from('<II',data,off);off+=8;props={}
        for _ in range(np):
            name=data[off:off+32].split(b'\0')[0].decode()
            size=struct.unpack_from('<I',data,off+32)[0]&0x7fffffff;off+=36
            assert name not in props and off+size<=len(data)
            props[name]=(off,data[off:off+size]);off+=(size+3)&~3
        path=parent+'/'+props['name'][1].split(b'\0')[0].decode()
        assert path not in nodes
        nodes[path]=props
        metadata[path]=(start,np,off)
        for _ in range(nc):off=walk(off,path)
        return off
    assert walk(0,'')==len(data)
    bus='/device-tree/arm-io'
    children=sorted(n for n in nodes if n.startswith(bus+'/'))
    assert children==[bus+'/intc',bus+'/ufshc',bus+'/ufsphy']
    off,value=nodes[bus]['ranges'];assert struct.unpack('<3Q',value)==(0,0,0x100000000)
    base,size=0x1000000,0x16000000
    output=bytearray(data);output[off:off+24]=struct.pack('<3Q',0,base,size)
    edits=[(off,off+24)];ranges=[]
    expected={'intc':[(0x16000000,0x10000),(0x16080000,0x200000)],
              'ufshc':[(0x1d84000,0x3000)],'ufsphy':[(0x1d80000,0x2000)]}
    for path in children:
        off,value=nodes[path]['reg'];assert len(value)%16==0
        pairs=list(struct.iter_unpack('<QQ',value));assert pairs==expected[path.rsplit('/',1)[1]]
        for i,(address,length) in enumerate(pairs):
            assert base<=address and address+length<=base+size
            at=off+i*16;struct.pack_into('<Q',output,at,address-base);edits.append((at,at+8))
            assert struct.unpack_from('<Q',output,at)[0]+base==address
            ranges.append({'node':path,'physical_before':hex(address),'physical_after':hex(base+address-base),'size':hex(length),'new_relative':hex(address-base)})
    assert all(a==b or any(lo<=i<hi for lo,hi in edits) for i,(a,b) in enumerate(zip(data,output)))
    if args.interrupt_alias:
        intc=bus+'/intc';start,count,end=metadata[intc]
        assert nodes[intc]['phandle'][1]==struct.pack('<I',1) and 'AAPL,phandle' not in nodes[intc]
        assert nodes[bus+'/ufshc']['interrupt-parent'][1]==nodes[intc]['phandle'][1]
        assert nodes[intc]['#interrupt-cells'][1]==struct.pack('<I',3)
        assert len(nodes[bus+'/ufshc']['interrupts'][1])==12
        prop=b'AAPL,phandle'.ljust(32,b'\0')+struct.pack('<I',4)+nodes[intc]['phandle'][1]
        output=output[:end]+prop+output[end:]
        struct.pack_into('<I',output,start,count+1)
    dest=ROOT/('edl_work/arm-io-phandle-fix-20261106' if args.interrupt_alias else 'edl_work/arm-io-fix-20261106');dest.mkdir(exist_ok=True)
    (dest/'kernel.macho.adt').write_bytes(output)
    report={'source_sha256':hashlib.sha256(data).hexdigest(),'output_sha256':hashlib.sha256(output).hexdigest(),
            'bytes':len(output),'arm_io_origin':hex(base),'arm_io_size':hex(size),'mmio_equivalence':ranges,
            'other_properties_unchanged':True,'AAPL_phandle_added':args.interrupt_alias,
            'source':'upstream 33b5508 docs/PHASE3-STORAGE.md; XNU IODeviceTreeSupport.cpp AddPHandle'}
    (dest/'reproduction-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
