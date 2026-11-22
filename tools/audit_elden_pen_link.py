"""Check the linked pen's original/native HID edges without changing global
experimental prelink qualification flags or claiming hardware acceptance.
"""
from pathlib import Path
import argparse,bisect,hashlib,json,re,struct,subprocess,sys
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();im=macho(raw);names={n:v for v,group in exports(raw,im).items() for n in group}
    exported=set(names)
    def read(address,size):
        offset=backed_offset(im['segments'],address,size,len(raw));return raw[offset:offset+size]
    # Inline virtuals are local symbols in the linked native image; the
    # prelink export dictionary intentionally omits them. Read the real native
    # symbol table and bind their bytes to the final image, not a name guess.
    native_path=a.kernel_build/'native.macho';native=native_path.read_bytes();native_image=macho(native)
    report=json.loads((a.kernel_build/'result.json').read_text())
    if hashlib.sha256(native).hexdigest()!=report['native_sha256']:raise ValueError('Native image identity differs')
    text_symbols=[];nm_named={};nm_all=[]
    for line in subprocess.check_output(['xcrun','nm','-n',str(native_path)],text=True).splitlines():
        match=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if not match:continue
        address=int(match[1],16);text_symbols.append(address);nm_named[match[2]]=address;nm_all.append((address,match[2]))
        if '11EldenPenHid' in match[2] or '_elden_pen_bridge_' in match[2] or '_elden_pen_owner_' in match[2] or '_elden_pen_route_' in match[2]:
            if match[2] in names and names[match[2]]!=address:raise ValueError('Native/prelink symbol address differs')
            names[match[2]]=address
    text_symbols.sort()
    parent=struct.unpack('<207Q',read(0xfffffff005d045c0,207*8))
    child=struct.unpack('<207Q',read(names['__ZTV11EldenPenHid']+16,207*8))
    differences=[i for i in range(207) if parent[i]!=child[i]]
    expected={0:'__ZN11EldenPenHidD1Ev',1:'__ZN11EldenPenHidD0Ev',7:'__ZNK11EldenPenHid12getMetaClassEv',
              14:'__ZN11EldenPenHid4freeEv',86:'__ZN11EldenPenHid5startEP9IOService',87:'__ZN11EldenPenHid4stopEP9IOService',
              111:'__ZNK11EldenPenHid11getWorkLoopEv',171:'__ZNK11EldenPenHid18newTransportStringEv',
              173:'__ZNK11EldenPenHid16newProductStringEv',180:'__ZNK11EldenPenHid19newReportDescriptorEPP18IOMemoryDescriptor'}
    if differences!=sorted(expected):raise ValueError('Unexpected pen vtable override slots')
    matched={}
    for slot,name in expected.items():
        if name not in names or child[slot]!=names[name]:raise ValueError('Wrong method at slot '+str(slot)+': '+name)
        address=names[name];at=backed_offset(native_image['segments'],address,32,len(native))
        if native[at:at+32]!=read(address,32):raise ValueError('Native method code changed in prelink')
        matched[str(slot)]=name
    base_meta=names['__ZN11IOHIDDevice10gMetaClassE']
    if struct.unpack('<Q',read(names['__ZN11EldenPenHid10superClassE'],8))[0]!=base_meta:raise ValueError('Wrong HID superclass')
    initializers=[]
    for segment in im['segments']:
        for section in segment['sections']:
            if section['name']=='__mod_init_func':
                data=raw[section['fileoff']:section['fileoff']+section['size']]
                initializers+=list(struct.unpack('<%dQ'%(len(data)//8),data))
    late=names['__GLOBAL__sub_I_EldenPenHid.cpp']
    if late in initializers:raise ValueError('Pen metaclass still initializes automatically')
    required=['_elden_pen_native_start','_elden_pen_native_packet','_elden_pen_native_fault','_elden_pen_native_stop',
              '_elden_pen_native_pause','_elden_pen_native_resume',
              '_elden_pen_hid_report','_elden_pen_pressure_fixed','_elden_touch_runtime_start','_elden_touch_request_stop']
    bridge=['_elden_pen_owner_init','_elden_pen_owner_bind','_elden_pen_owner_packet','_elden_pen_owner_tick',
            '_elden_pen_owner_fault','_elden_pen_owner_pause','_elden_pen_owner_resume','_elden_pen_owner_stop',
            '_elden_pen_owner_release','_elden_pen_owner_retire','_elden_pen_owner_released','_elden_pen_owner_status',
            '_elden_pen_bridge_init','_elden_pen_bridge_bind','_elden_pen_bridge_start','_elden_pen_bridge_packet',
            '_elden_pen_bridge_tick','_elden_pen_bridge_fault','_elden_pen_bridge_pause','_elden_pen_bridge_resume',
            '_elden_pen_bridge_stop','_elden_pen_bridge_release','_elden_pen_bridge_retire','_elden_pen_bridge_status',
            '_elden_pen_bridge_started','_elden_pen_bridge_bound',
            '_elden_pen_route_begin','_elden_pen_route_packet','_elden_pen_route_tick','_elden_pen_route_fault',
            '_elden_pen_route_pause','_elden_pen_route_resume','_elden_pen_route_stop','_elden_pen_route_release',
            '_elden_pen_route_retire','_elden_pen_route_status']
    if any(n not in names for n in required+bridge):raise ValueError('Missing pen/touch interface')
    if any(n not in exported for n in bridge):raise ValueError('Guarded pen entry absent from prelink export table')
    # The guarded owner/route path ships as ordinary scalar objects wired to
    # the real native entry: entries are exported, none may initialize, and
    # the only legitimate direct callers are the entries themselves, the
    # elden_pen_native_* entry surface and its sendPen/released helpers.
    entries_k=sorted(names[n] for n in bridge)
    if any(address in initializers for address in entries_k):raise ValueError('Guarded pen entry initializes automatically')
    # Call-graph checks run in native-image space; the kernel image supplies
    # initializer/data-pointer checks against its own bound addresses.
    entries=sorted(nm_named[n] for n in bridge);entry_set=set(entries)
    def next_symbol(address):
        at=bisect.bisect_right(text_symbols,address)
        return text_symbols[at] if at<len(text_symbols) else 1<<64
    named_sorted=sorted(nm_named.items(),key=lambda kv:kv[1])
    text_addrs=[a for _,a in named_sorted]
    def owner_of(pc):
        at=bisect.bisect_right(text_addrs,pc)-1
        return named_sorted[at][0] if at>=0 else ''
    allowed=set(bridge)|{'_elden_pen_native_start','_elden_pen_native_packet','_elden_pen_native_fault',
        '_elden_pen_native_stop','_elden_pen_native_pause','_elden_pen_native_resume'}
    def allowed_caller(c):return c in allowed or 'sendPen' in c or 'released' in c
    calls=[]
    for segment in native_image['segments']:
        if segment['name']=='__LINKEDIT':continue
        executable=bool(segment['initprot']&4)
        for section in segment['sections']:
            body=native[section['fileoff']:section['fileoff']+section['size']]
            if executable:
                for i,word in enumerate(struct.unpack('<%dI'%(len(body)//4),body[:len(body)//4*4])):
                    if (word&0x7c000000)==0x14000000:
                        offset=word&0x3ffffff
                        if offset&0x2000000:offset-=0x4000000
                        target=section['va']+i*4+offset*4;pc=section['va']+i*4
                        calls.append((pc,target,owner_of(pc)))
                        if target in entry_set and not allowed_caller(owner_of(pc)):
                            raise ValueError('Foreign call to guarded pen entry '+hex(target)+' from '+owner_of(pc))
    for segment in im['segments']:
        if segment['name']=='__LINKEDIT' or segment['initprot']&4:continue
        for section in segment['sections']:
            body=raw[section['fileoff']:section['fileoff']+section['size']]
            for address in entries_k:
                if body.find(struct.pack('<Q',address))!=-1:raise ValueError('Guarded pen entry referenced by '+segment['name']+'/'+section['name'])
    # The positive edge: the derived worker's anonymous-namespace loop must
    # call the native entry, the native entry must call the route, the route
    # must call the bridge, the bridge must call the owner, and the owner must
    # reach the existing decode/feed and the bound send into the HID dispatch.
    def edge(caller,target):
        address=nm_named.get(target)
        return address is not None and any(t==address and c==caller for _,t,c in calls)
    required_edges=[('_elden_pen_native_packet','_elden_pen_route_packet'),
        ('_elden_pen_native_start','_elden_pen_route_begin'),('_elden_pen_native_fault','_elden_pen_route_fault'),
        ('_elden_pen_native_pause','_elden_pen_route_pause'),('_elden_pen_native_resume','_elden_pen_route_resume'),
        ('_elden_pen_native_stop','_elden_pen_route_stop'),('_elden_pen_native_stop','_elden_pen_route_release'),
        ('_elden_pen_native_stop','_elden_pen_route_retire'),('_elden_pen_native_stop','_elden_pen_route_status'),
        ('_elden_pen_route_packet','_elden_pen_bridge_packet'),('_elden_pen_route_begin','_elden_pen_bridge_init'),
        ('_elden_pen_route_begin','_elden_pen_bridge_bind'),('_elden_pen_route_begin','_elden_pen_bridge_start'),
        ('_elden_pen_route_stop','_elden_pen_bridge_stop'),('_elden_pen_route_retire','_elden_pen_bridge_retire'),
        ('_elden_pen_route_release','_elden_pen_bridge_release'),('_elden_pen_route_status','_elden_pen_bridge_status'),
        ('_elden_pen_route_fault','_elden_pen_bridge_fault'),('_elden_pen_route_pause','_elden_pen_bridge_pause'),
        ('_elden_pen_route_resume','_elden_pen_bridge_resume'),('_elden_pen_route_tick','_elden_pen_bridge_tick'),
        ('_elden_pen_bridge_packet','_elden_pen_owner_packet'),
        ('_elden_pen_owner_packet','_elden_pen_decode'),('_elden_pen_owner_packet','_elden_pen_delivery_feed')]
    for caller,target in required_edges:
        if not edge(caller,target):raise ValueError('Worker-route edge absent: '+caller+' -> '+target)
    # Every native entry call must originate inside the derived touch worker's
    # own translation unit: its exported _elden_touch_* entries or the
    # anonymous-namespace locals (worker/finish/…) that cluster with them.
    touch_member=lambda n:n in ('_elden_touch_runtime_start','_elden_touch_request_stop') or (
        '_GLOBAL__N_1' in n and re.search(r'(7Context|15EldenTouch|elden_nvt|workerEPvi|survey_smmu|dma_check|dma_idle|dma_release_proof|release_contacts|prepare_dma|transferEPvPKhPhm|sameERK)',n))
    touch_addrs=[a for a,n in nm_all if touch_member(n)]
    if not touch_addrs:raise ValueError('Derived worker text not found in image')
    touch_lo,touch_hi=min(touch_addrs),max(touch_addrs)
    def worker_caller(pc,c):
        return c in ('_elden_touch_runtime_start','_elden_touch_request_stop') or (
            '_GLOBAL__N_1' in c and touch_lo<=pc<=touch_hi)
    for n in ('_elden_pen_native_start','_elden_pen_native_packet','_elden_pen_native_stop','_elden_pen_native_fault'):
        if not any(t==nm_named[n] and worker_caller(pc,c) for pc,t,c in calls):
            raise ValueError('Derived worker call absent: '+n)
    if not any(t==nm_named['_elden_pen_hid_report'] and '11EldenPenHid' in c for _,t,c in calls):
        raise ValueError('HID dispatch edge absent')
    # The owner pulls through real provider callbacks: inside route_begin the
    # four static callbacks must be materialized by adrp+add (they are stored
    # into the provider struct the bind consumes — an indirect BLR edge no
    # static call scan could name). Statics are local symbols in the TU.
    lo=nm_named['_elden_pen_route_begin'];hi=next_symbol(lo)
    regs={};materialized=set()
    for segment in native_image['segments']:
        if not (segment['initprot']&4):continue
        for section in segment['sections']:
            body=native[section['fileoff']:section['fileoff']+section['size']]
            for i,word in enumerate(struct.unpack('<%dI'%(len(body)//4),body[:len(body)//4*4])):
                pc=section['va']+i*4
                if not (lo<=pc<hi):continue
                if (word&0x9f000000)==0x90000000:        # adrp
                    imm=((word>>5)&0x7ffff)<<2|(word>>29)&3
                    if imm&(1<<20):imm-=1<<21
                    regs[word&31]=(pc&~0xfff)+(imm<<12)
                elif (word&0x7f000000)==0x11000000:     # add/sub imm64
                    rd,rn,imm,sh=word&31,(word>>5)&31,(word>>10)&0xfff,(word>>22)&3
                    if rn in regs:
                        value=regs[rn]+(imm<<(12*sh));materialized.add(value)
                        if rd!=rn:regs.pop(rd,None)
                elif (word&0xffe00000)!=0xf9400000:     # any other rd write kills tracking
                    regs.pop(word&31,None)
    route_span_lo=min(nm_named[n] for n in bridge if n.startswith('_elden_pen_route_'))
    route_span_hi=max(nm_named[n] for n in bridge if n.startswith('_elden_pen_route_'))
    local_in_route={a for a,n in nm_all if n.lstrip('_').startswith('rt_') and route_span_lo<=a<=route_span_hi}
    if len(local_in_route)<4 or not all(a in materialized for a in local_in_route):
        raise ValueError('Route provider callbacks not materialized in begin')
    meta=json.loads((a.kernel_build/'kernel.json').read_text())
    result={'pen_specific_abi_checks_passed':True,'kernel_sha256':hashlib.sha256(raw).hexdigest(),
            'device_vtable_slots':207,'override_slots':differences,'override_methods':matched,'original_superclass':hex(base_meta),
            'late_initializer_not_automatic':True,'interfaces':{n:hex(names[n]) for n in required},
            'bridge_exports':{n:hex(names[n]) for n in bridge},
            'worker_route_owner_delivery_hid_edges':['%s -> %s'%(c,t) for c,t in required_edges],
            'provider_callbacks_materialized_in_begin':sorted(hex(v) for v in local_in_route),
            'entries_not_automatically_initialized':True,'no_foreign_callers':True,'no_data_references':True,
            'legacy_flags_unchanged':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')},
            'native_bound':False,'pencil_accepted':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: linked pen HID inheritance,207slots/10overrides,late initialization,worker->route->owner->delivery->HID edges and guarded entries')
if __name__=='__main__':main()
