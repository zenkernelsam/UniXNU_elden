"""Offline RAM-window planning from an FDT snapshot and optional EFI map.

No memory map is changed. A DRAM-only result is not proof EFI can allocate it.
All static reservations, memreserve entries and original initrd are excluded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

LIMIT=1<<64
MIB=1<<20


def parse_fdt(data):
    if len(data)<40:raise ValueError('short FDT')
    magic,total,structure,strings,reserve,version,compatible,_,string_bytes,struct_bytes=struct.unpack_from('>10I',data)
    if magic!=0xd00dfeed or total>len(data) or total<40 or version<17 or compatible>17:
        raise ValueError('invalid FDT header')
    if any(offset<40 or offset>total or size>total-offset for offset,size in [(structure,struct_bytes),(strings,string_bytes)]):
        raise ValueError('FDT region outside input')
    if structure%4 or reserve%8 or reserve<40:raise ValueError('unaligned FDT region')
    reservations=[];at=reserve
    while at+16<=min(structure,strings):
        start,size=struct.unpack_from('>QQ',data,at);at+=16
        if start==size==0:break
        if not size or start+size>LIMIT:raise ValueError('invalid memreserve range')
        reservations.append((start,start+size,'FDT memreserve'))
    else:raise ValueError('unterminated memreserve')
    nodes={};stack=[];at=structure;end=at+struct_bytes
    while at+4<=end:
        token=struct.unpack_from('>I',data,at)[0];at+=4
        if token==1:
            stop=data.find(b'\0',at,end)
            if stop<0 or stop-at>255:raise ValueError('invalid node name')
            name=data[at:stop].decode('ascii');at=(stop+4)&~3
            if not stack and name:raise ValueError('nonempty root name')
            path=('/'+'/'.join([*stack[1:],name])).rstrip('/') or '/'
            if path in nodes:raise ValueError('duplicate node path')
            nodes[path]={};stack.append(name)
            if len(stack)>128:raise ValueError('excessive FDT depth')
        elif token==2:
            if not stack:raise ValueError('unbalanced FDT')
            stack.pop()
        elif token==3:
            if not stack or at+8>end:raise ValueError('property outside node')
            length,nameoff=struct.unpack_from('>II',data,at);at+=8
            if length>end-at or nameoff>=string_bytes:raise ValueError('property bounds')
            stop=data.find(b'\0',strings+nameoff,strings+string_bytes)
            if stop<0:raise ValueError('unterminated property name')
            name=data[strings+nameoff:stop].decode('ascii');path=('/'+'/'.join(stack[1:])).rstrip('/') or '/'
            if name in nodes[path]:raise ValueError('duplicate property')
            nodes[path][name]=data[at:at+length];at=(at+length+3)&~3
        elif token==4:continue
        elif token==9:
            if stack or at!=end:raise ValueError('invalid FDT end')
            return nodes,reservations
        else:raise ValueError('unsupported FDT token')
    raise ValueError('FDT end missing')


def u32(data):
    if len(data)!=4:raise ValueError('cell property must be u32')
    return int.from_bytes(data,'big')


def ranges(nodes,path,allow_zero=False):
    parent=path.rsplit('/',1)[0] or '/';props=nodes[parent]
    ac=u32(props.get('#address-cells',nodes['/'].get('#address-cells',b'\0\0\0\2')))
    sc=u32(props.get('#size-cells',nodes['/'].get('#size-cells',b'\0\0\0\1')))
    if ac not in (1,2) or sc not in (1,2):raise ValueError('unsupported address/size cells')
    blob=nodes[path].get('reg',b'');stride=4*(ac+sc)
    if not blob or len(blob)%stride:raise ValueError('invalid reg tuple')
    result=[]
    for at in range(0,len(blob),stride):
        start=int.from_bytes(blob[at:at+4*ac],'big');size=int.from_bytes(blob[at+4*ac:at+stride],'big')
        if not size and allow_zero:continue # ABL may retain inactive memory-bank slots.
        if not size or start+size>LIMIT:raise ValueError('invalid address range')
        result.append((start,start+size,path))
    return result


def union(items):
    result=[]
    for start,end,*_ in sorted(items):
        if start<0 or end<=start or end>LIMIT:raise ValueError('invalid interval')
        if result and start<=result[-1][1]:result[-1]=(result[-1][0],max(end,result[-1][1]))
        else:result.append((start,end))
    return result


def subtract(memory,reserved):
    result=union(memory)
    for cut_start,cut_end in union(reserved):
        next_result=[]
        for start,end in result:
            if cut_end<=start or cut_start>=end:next_result.append((start,end));continue
            if start<cut_start:next_result.append((start,cut_start))
            if cut_end<end:next_result.append((cut_end,end))
        result=next_result
    return result


def inspect(data):
    nodes,reserved=parse_fdt(data);memory=[];unresolved=[]
    for path,props in nodes.items():
        if props.get('device_type',b'').rstrip(b'\0')==b'memory':memory+=ranges(nodes,path,allow_zero=True)
        if path.startswith('/reserved-memory/'):
            if 'reg' in props:
                known=ranges(nodes,path,allow_zero=True)
                reserved+=known
                if not known and props.get('status',b'').rstrip(b'\0')!=b'disabled':
                    unresolved.append(path+' (zero-size placeholder)')
            elif 'size' in props:unresolved.append(path)
        elif 'framebuffer' in path and 'reg' in props:reserved+=ranges(nodes,path)
    chosen=nodes.get('/chosen',{})
    if 'linux,initrd-start' in chosen or 'linux,initrd-end' in chosen:
        a=chosen.get('linux,initrd-start',b'');b=chosen.get('linux,initrd-end',b'')
        if len(a) not in (4,8) or len(b) not in (4,8):raise ValueError('incomplete initrd range')
        start,end=int.from_bytes(a,'big'),int.from_bytes(b,'big')
        if not start or end<=start:raise ValueError('invalid initrd range')
        reserved.append((start,end,'/chosen original initrd'))
    if not memory:raise ValueError('no physical memory nodes')
    return memory,reserved,unresolved


def efi_ranges(path):
    text=path.read_text(encoding='utf-8')
    if text.lstrip().startswith('['):
        records=json.loads(text);result=[]
        for item in records:
            if item.get('type',7)!=7:continue
            value=lambda key:int(item[key],0) if isinstance(item[key],str) else item[key]
            base,size=value('base'),value('bytes');result.append((base,base+size))
    else:
        result=[(int(a,16),int(a,16)+int(b,16)) for a,b in re.findall(
            r'ELDEN_MEMORY: conventional base=0x([0-9a-fA-F]+) bytes=0x([0-9a-fA-F]+)',text)]
    if not result:raise ValueError('no EFI conventional-memory records')
    return union(result)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fdt',type=Path,required=True)
    p.add_argument('--efi-map',type=Path)
    p.add_argument('--window-mib',type=int,default=4096)
    p.add_argument('--ram-data-mib',type=int,default=0)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if not 256<=a.window_mib<=8192 or a.window_mib%2 or a.ram_data_mib<0 or a.ram_data_mib>=a.window_mib:
        p.error('window must be 256..8192 MiB, 2MiB aligned; RAM Data must fit within it')
    data=a.fdt.read_bytes();memory,reserved,unresolved=inspect(data);free=subtract(memory,reserved)
    if a.efi_map:
        allowed=efi_ranges(a.efi_map)
        free=union([(max(x,a),min(y,b)) for x,y in free for a,b in allowed if max(x,a)<min(y,b)])
    requested=a.window_mib*MIB;candidates=[]
    for start,end in free:
        base=(start+2*MIB-1)&~(2*MIB-1)
        if end-base>=requested:candidates.append(dict(base=hex(base),bytes=requested,remaining_in_extent=end-base-requested))
    format_ranges=lambda items:[dict(base=hex(x),end_exclusive=hex(y),bytes=y-x,source=rest[0] if rest else None) for x,y,*rest in items]
    report=dict(mode='offline planning; no allocation or firmware change',fdt_sha256=hashlib.sha256(data).hexdigest(),
        requested_window_bytes=requested,ram_data_bytes=a.ram_data_mib*MIB,
        DRAM=format_ranges(memory),excluded=format_ranges(reserved),unresolved_reservations=unresolved,
        candidate_windows=candidates,efi_map_supplied=bool(a.efi_map),hardware_qualified=False,
        required=['fresh FDT and EFI map from the same boot','resolve dynamic reservations',
                  'actual EFI allocation and handoff validation','kernel/VM/userland working-set budget beyond image file size'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('DRAM','excluded')},indent=2))


if __name__=='__main__':main()
