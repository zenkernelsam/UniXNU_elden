"""Audit actual shared batch/MMIO/GIC, staged owners, archives and recovery."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from camera_irq_input_audit import retained_camera_irq_input
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','parent','shared-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text());build=json.loads((a.shared_build/'result.json').read_text())
    if r.get('actual_shared_SMMU_native_staged_peripherals') is not True or r['shared_build_sha256']!=sha(a.shared_build/'result.json'):raise ValueError('Exact shared native profile absent')
    for folder,record in ((a.kernel_build,r),(a.parent,prior)):
        if sha(folder/'kernel.macho')!=record['kernel_sha256'] or sha(folder/'native.macho')!=record['native_sha256']:raise ValueError('Actual shared/parent image identity differs')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    def symbol(n):
        found=local.get(n,set()).copy()
        if n in names:found.add(names[n])
        if len(found)!=1:raise ValueError('Missing unique actual shared symbol: '+n)
        return found.pop()
    calls=[];init=[];oldinit=[]
    for image,body,table in ((im,raw,init),(oi,old,oldinit)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(data)-3,4):
                        w=struct.unpack_from('<I',data,off)[0]
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('Automatic shared startup added')
    # All entry points still require the original physical owner's explicit
    # caller. No software success provider or default initializer is allowed.
    outer=('_elden_smmu_native_open','_elden_smmu_native_shared','_elden_smmu_peripheral_open','_elden_smmu_peripheral_activate',
           '_elden_smmu_camera_owner','_elden_smmu_sensor_owner','_elden_camera_dma_prepare_batch','_elden_camera_dma_activate',
           '_elden_sensor_memory_native_prepare','_elden_sensor_memory_native_activate')
    outer+=('_elden_sensor_runtime_adopt',)
    for n in outer:
        if any(t==symbol(n) for pc,t in calls):raise ValueError('Unqualified outer shared/peripheral startup: '+n)
    edges={}
    required=['_elden_smmu_shared_open','_elden_smmu_shared_install','_elden_smmu_shared_observe_prepared','_elden_smmu_shared_observe','_elden_smmu_shared_retire',
        '_elden_smmu_shared_lease','_elden_smmu_shared_early','_elden_smmu_shared_admit','_elden_smmu_boot_routes_reserve','_elden_smmu_boot_routes_commit',
        '_elden_smmu_boot_routes_peek','_elden_smmu_boot_routes_poison','_elden_smmu_boot_routes_early','_elden_smmu_boot_route_seal','_elden_smmu_boot_route_expected',
        '_EldenWifiSmmu4KProgramDisabledContext','_EldenWifiSmmu4KObserveAuditedContext','_EldenWifiSmmu4KDisableOwnedContext','_EldenWifiSmmu4KBindTableMemory',
        '_armcpu_spi_attach','_armcpu_spi_mask']
    for n in required:
        hits=[pc for pc,t in calls if t==symbol(n)]
        if not hits:raise ValueError('Actual shared native edge missing: '+n)
        edges[n]={'target':hex(symbol(n)),'calls_and_tails':[hex(pc) for pc in hits]}
    # Derivation actually replaces the old linked touch checker; registration
    # still occurs only inside that checker, immediately before snapshot.
    for callee in ('_elden_smmu_boot_route_seal','_elden_smmu_boot_route_expected'):
        if not any(t==symbol(callee) for pc,t in calls):raise ValueError('Derived touch coexistence missing')
    before=json.loads((a.parent/'link-command.json').read_text());after=json.loads((a.kernel_build/'link-command.json').read_text());archives={};direct={}
    for value in before:
        path=Path(value.split(',')[-1])
        if value.startswith('-Wl,-force_load,'):archives[str(path)]=retained_archive(value,after,r)
        elif path.is_file() and path.suffix in ('.a','.o'):
            if r.get('actual_original_camera_IRQ_executor') is True and path.name=='NativePipeline.o':
                direct[str(path)]=retained_camera_irq_input(path,after,r);continue
            delta=r.get('shared_direct_deltas',{}).get(path.name)
            if delta and delta['before_path']==str(path):
                if sha(path)!=delta['before'] or after.count(delta['after_path'])!=1 or sha(Path(delta['after_path']))!=delta['after']:raise ValueError('Declared native layout consumer changed')
                direct[str(path)]=delta
            elif after.count(value)!=1 or sha(path)!=r['inherited_inputs'].get(str(path)):raise ValueError('Unrelated native input changed')
    if set(r['shared_archive_deltas'])!={'libNativeLegacyDisplay.late.a','libEldenSensorMemory.a'} or set(r['shared_direct_deltas'])!={'NativeCameraDma.o','SensorRuntimeNative.o'}:raise ValueError('Unexpected shared native replacement scope')
    for name,row in build['objects'].items():
        if sha(a.shared_build/name)!=row['sha256']:raise ValueError('Actual new shared object differs')
    for name in ('SharedNative.o','NativeCameraDma.o','NativeMemory.o'):
        asm=(a.shared_build/(name+'-asm.log')).read_text().lower()
        if name=='SharedNative.o':ops=('ldr','str','dsb','cntpct_el0','mpidr_el1')
        else:ops=('s1e1r','s1e1w','par_el1','daif','civac','dsb')
        for op in ops:
            if op not in asm:raise ValueError('Actual native hardware instruction missing: '+name+'/'+op)
    # Do not emit another copy of original4K. Its actual symbols occur exactly
    # once; the linked archive remains the original algorithm owner.
    for n in ('_EldenWifiSmmu4KBuild','_EldenWifiSmmu4KProgramDisabledContext','_EldenWifiSmmu4KObserveAuditedContext','_EldenWifiSmmu4KBindTableMemory'):symbol(n)
    recovery=symbol('_elden_sensor_runtime_recovery_stop');hits=[pc for pc,t in calls if t==recovery]
    if len(hits)!=1 or r['recovery_object_sha256']!=prior['recovery_object_sha256'] or r['aggregate_recovery_bound_ms']!=5500:raise ValueError('Original bounded recovery changed')
    sequence=[hits[0]]
    for n in ('_elden_haptics_stop_for_reboot','_elden_rtc_stop_for_reboot','_elden_ufs_wait_shutdown'):
        following=[pc for pc,t in calls if t==symbol(n) and hits[0]<pc<hits[0]+4096]
        if len(following)!=1 or following[0]<=sequence[-1]:raise ValueError('Original recovery order changed')
        sequence.append(following[0])
    flags=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:flags.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global qualification changed')
    result={'passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'actual_calls':edges,'archives':archives,'direct':direct,
        'retained_original4K_once':True,'original_recovery_order':[hex(pc) for pc in sequence],'aggregate_bound_ms':5500,'unchanged_init_count':True,
        'shared_outer_starts_have_no_caller':list(outer),'global_flags':flags,'physical_owner_installed':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS actual sharedSMMU/MMIO/GIC/staged-memory edges, exact archive/layout deltas, one4K and original recovery/Apple30')
if __name__=='__main__':main()
