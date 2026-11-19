"""Audit actual sensor memory direct IOKit ABI and full table/RPC call edges."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','parent','memory-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text());built=json.loads((a.memory_build/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=r['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=r['native_sha256'] or sha(a.parent/'kernel.macho')!=prior['kernel_sha256']:raise ValueError('Memory image identity differs')
    im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('Unique actual memory symbol missing: '+n)
        return values.pop()
    calls=[];init=[];oldinit=[]
    for image,b,table in [(im,raw,init),(oi,old,oldinit)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                data=b[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for at in range(0,len(data)-3,4):
                        word=struct.unpack_from('<I',data,at)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+at;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('Automatic memory initializer added')
    entry=symbol('_elden_sensor_memory_binding_open')
    if any(t==entry for pc,t in calls):raise ValueError('Unqualified owner starts sensor memory automatically')
    edges={}
    required=['_elden_sensor_memory_native_open','_elden_sensor_memory_native_view','_elden_sensor_memory_native_admit','_elden_sensor_memory_native_publish','_elden_sensor_memory_native_stop',
        '_elden_sensor_pack_binding_open','_elden_sensor_pack_binding_step','_elden_sensor_pack_binding_finish','_elden_sensor_pack_binding_receive',
        '_EldenWifiSmmu4KBuild','_EldenWifiSmmu4KAuditTables','_EldenWifiSmmu4KBindTableMemory','_EldenWifiSmmu4KEncodeContext',
        '__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm','__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv',
        '__ZN25IOGeneralMemoryDescriptor7prepareEj','__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj',
        '__ZN18IOMemoryDescriptor19createMappingInTaskEP4taskyjyy','__ZN11IOMemoryMap17getVirtualAddressEv']
    for n in required:
        target=symbol(n);hits=[hex(pc) for pc,t in calls if t==target]
        if not hits:raise ValueError('Actual memory/table/RPC call absent: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    # The actual original IOKit method table slots, NOT the captured subclass
    # getBytesNoCopy slot, remain in the final OS image. Native uses direct calls.
    def u64(va):return struct.unpack_from('<Q',raw,backed_offset(im['segments'],va,8,len(raw)))[0]
    base=symbol('__ZTV24IOBufferMemoryDescriptor')+16
    if u64(base+27*8)!=symbol('__ZN25IOGeneralMemoryDescriptor7prepareEj'):raise ValueError('Original prepare slot changed')
    if u64(base+43*8)==symbol('__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv'):raise ValueError('Incorrect captured byte slot unexpectedly accepted')
    dis=(a.memory_build/'NativeMemory-asm.log').read_text()
    for op in ('s1e1r','s1e1w','par_el1','daif','civac','dsb'):
        if op not in dis.lower():raise ValueError('Actual memory/PAR/cache instruction absent: '+op)
    if built['objects']['NativeMemory.o']['sha256']!=sha(a.memory_build/'NativeMemory.o'):raise ValueError('Actual allocator object changed')
    for n in ('__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv','__ZN25IOGeneralMemoryDescriptor7prepareEj','__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj','__ZN11IOMemoryMap17getVirtualAddressEv'):
        if n not in built['objects']['NativeMemory.o']['imports']:raise ValueError('Direct original ABI import absent')
    before=json.loads((a.parent/'link-command.json').read_text());after=json.loads((a.kernel_build/'link-command.json').read_text())
    retained={}
    for v in before:
        if v.startswith('-Wl,-force_load,'):retained[Path(v.split(',',2)[2]).name]=retained_archive(v,after,r)
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental flags changed')
    result={'native_memory_table_RPC_component_link_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],
        'actual_direct_original_Iokit_and_table_RPC_calls':edges,'original_prepare_slot':27,'captured_bytes_slot43_rejected':True,
        'PAR_read_write_restore_cache_instructions_present':True,'original_archive_inputs_and_init_count_retained':True,'actual_retained_archive_checks':retained,'no_new_startup':True,
        'shared_actual_SMMU_GLINK_owner_or_Y700_capture':False,'hardware':False,'global_flags':flags}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual original IOKit/PAR/cache/4K table→pack→RPC paths, unchanged owners/initializers; no new startup')
if __name__=='__main__':main()
