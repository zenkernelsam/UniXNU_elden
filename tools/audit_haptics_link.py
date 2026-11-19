"""Check linked haptics IOService/IOUserClient inheritance and actual call edges."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();im=macho(raw);report=json.loads((a.kernel_build/'result.json').read_text())
    if hashlib.sha256(raw).hexdigest()!=report['kernel_sha256']:raise ValueError('Kernel digest differs')
    names={n:v for v,group in exports(raw,im).items() for n in group};local={}
    native=a.kernel_build/'native.macho';data=native.read_bytes();ni=macho(data)
    if hashlib.sha256(data).hexdigest()!=report['native_sha256']:raise ValueError('Native digest differs')
    symbol_lines=subprocess.check_output(['xcrun','nm','-n',native],text=True).splitlines()
    for line in symbol_lines:
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:
            addr=int(m[1],16);local.setdefault(m[2],set()).add(addr)
    def read(addr,n):
        off=backed_offset(im['segments'],addr,n,len(raw));return raw[off:off+n]
    def unique(name):
        hits=local.get(name,set()).copy()
        if name in names:hits.add(names[name])
        if len(hits)!=1:raise ValueError('Unique symbol required: '+name)
        return hits.pop()
    def table(name):
        base=unique(name)+16;out=[]
        for i in range(512):
            v=struct.unpack('<Q',read(base+i*8,8))[0]
            if not v:return out
            out.append(v)
        raise ValueError('Missing vtable boundary')
    classes={}
    for cls,parent,methods in [('18EldenHapticsClient','12IOUserClient',['D1Ev','D0Ev','4freeEv','5startEP9IOService','11clientCloseEv','14externalMethodE','19clientMemoryForTypeE']),
                               ('19EldenHapticsService','9IOService',['D1Ev','D0Ev','5startEP9IOService','13newUserClientE'])]:
        child=table('__ZTV'+cls);base=table('__ZTV'+parent)
        if len(child)!=len(base):raise ValueError('Unexpected inherited table extent')
        if struct.unpack('<Q',read(unique('__ZN'+cls+'10superClassE'),8))[0]!=unique('__ZN'+parent+'10gMetaClassE'):
            raise ValueError('Wrong real superclass')
        delta=[i for i in range(len(base)) if base[i]!=child[i]];matched={}
        for slot in delta:
            candidates=[n for n,addrs in local.items() if child[slot] in addrs and (n.startswith('__ZN'+cls) or n.startswith('__ZNK'+cls))]
            expected=[n for n in candidates if n.endswith('12getMetaClassEv') or any(n.startswith('__ZN'+cls+s) for s in methods)]
            if not expected:raise ValueError('Unexpected overriding method at '+str(slot))
            off=backed_offset(ni['segments'],child[slot],16,len(data))
            if data[off:off+16]!=read(child[slot],16):raise ValueError('Overriding method bytes changed')
            matched[str(slot)]=sorted(expected)
        # Service has two newUserClient overloads, all other expected methods
        # occur once; destructor aliases share a target but slots stay distinct.
        need=8 if cls.startswith('18') else 6
        if len(delta)!=need:raise ValueError('Unexpected override count')
        classes[cls]={'slots':len(base),'overrides':matched,'superclass':parent}
    late=unique('__GLOBAL__sub_I_EldenHapticsNative.cpp');initializers=[];calls=[]
    for seg in im['segments']:
        for sec in seg['sections']:
            block=raw[sec['fileoff']:sec['fileoff']+sec['size']]
            if sec['name']=='__mod_init_func':initializers+=list(struct.unpack('<%dQ'%(len(block)//8),block))
            if sec['name']=='__text':
                for off in range(0,len(block)-3,4):
                    word=struct.unpack_from('<I',block,off)[0]
                    if word&0xfc000000==0x94000000:
                        pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if late in initializers:raise ValueError('Haptics metaclasses still initialize automatically')
    start=unique('_elden_haptics_native_start');stop=unique('_elden_haptics_stop_for_reboot')
    starts=[pc for pc,target in calls if target==start];stops=[pc for pc,target in calls if target==stop]
    if len(starts)!=1 or len(stops)!=1:raise ValueError('One real startup/recovery call required')
    rtc=unique('_elden_rtc_stop_for_reboot');ufs=unique('_elden_ufs_wait_shutdown')
    rtc_calls=[pc for pc,target in calls if target==rtc and stops[0]<pc<stops[0]+4096]
    ufs_calls=[pc for pc,target in calls if target==ufs and stops[0]<pc<stops[0]+4096]
    if len(rtc_calls)!=1 or len(ufs_calls)!=1 or not stops[0]<rtc_calls[0]<ufs_calls[0]:raise ValueError('Actual recovery order differs')
    if len([pc for pc,target in calls if target==late])!=1:raise ValueError('One explicit late metaclass call required')
    actuator={}
    targets=['_get_bsdtask_info','_csproc_get_platform_binary','_proc_name','_proc_pid',
        '__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm',
        '__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv','_elden_actuator_profile_get',
        '_elden_actuator_live_init','_elden_actuator_live_new_run','_elden_actuator_live_step','_elden_actuator_live_stop',
        '_elden_broker_actuator_claim','_elden_broker_actuator_reserve','_elden_broker_actuator_settle','_elden_broker_actuator_release']
    for name in targets:
        target=unique(name);edges=[pc for pc,v in calls if v==target]
        if not edges:raise ValueError('Actual Actuator call missing: '+name)
        actuator[name]={'target':hex(target),'calls':[hex(pc) for pc in edges]}
    open_start=unique('_elden_haptics_actuator_open')
    # Final iOS linker emits __TEXT_EXEC symbols as S, not host nm's T.
    # The following exported wrapper is the exact compiled function boundary.
    open_end=unique('_elden_haptics_actuator_close')
    if open_end<=open_start:raise ValueError('Actual Actuator wrapper order differs')
    byte_target=unique('__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv')
    byte_calls=[pc for pc,v in calls if v==byte_target and open_start<=pc<open_end]
    if len(byte_calls)!=1:raise ValueError('Actual open must directly call the original IOBuffer bytes method')
    # The remaining open virtual is prepare(0), captured slot27. Qualifying a
    # getBytesNoCopy call still compiled a virtual at captured slot43 (appendBytes
    # in the original OS17 class), so future regressions must be refused here.
    buffer=unique('__ZTV24IOBufferMemoryDescriptor')+16
    prepare=struct.unpack('<Q',read(buffer+27*8,8))[0]
    if prepare!=unique('__ZN25IOGeneralMemoryDescriptor7prepareEj'):raise ValueError('Original IOBuffer prepare slot differs')
    open_words=[struct.unpack('<I',read(pc,4))[0] for pc in range(open_start,open_end,4)]
    if open_words.count(0xd63f0100)!=1 or 0xf9406d08 not in open_words or 0xf940ad08 in open_words:
        raise ValueError('Unexpected Actuator open virtual dispatch; exact prepare and direct bytes required')
    for name in ('_elden_haptics_actuator_info','_elden_haptics_actuator_request','_elden_haptics_actuator_memory'):
        unique(name)
    shared=unique('_sm8750_gcc_vote_owner');contract=unique('_elden_touch_shared_gcc_contract')
    meta=json.loads((a.kernel_build/'kernel.json').read_text())
    result={'haptics_component_abi_link_passed':True,'classes':classes,'late_initializer_not_automatic':True,
        'startup_call':hex(starts[0]),'recovery_calls':[hex(stops[0]),hex(rtc_calls[0]),hex(ufs_calls[0])],
        'shared_gcc_owner':hex(shared),'touch_shared_contract':hex(contract),'kernel_sha256':report['kernel_sha256'],
        'actual_actuator_calls':actuator,'actuator_open_original_buffer_bytes_call':hex(byte_calls[0]),
        'actuator_open_original_prepare_slot':27,
        'legacy_flags_unchanged':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')},'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual haptics service/client inheritance, late initializer, startup and stop→RTC→UFS calls')
if __name__=='__main__':main()
