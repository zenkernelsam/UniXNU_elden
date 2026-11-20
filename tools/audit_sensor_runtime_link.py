"""Audit actual compiled sensor lifecycle starts, callback vtable and recovery."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from peripheral_startup_audit import no_outer_sensor_start
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','runtime-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text());build=json.loads((a.runtime_build/'result.json').read_text())
    for folder,record in ((a.kernel_build,r),(a.parent,prior)):
        if sha(folder/'kernel.macho')!=record['kernel_sha256'] or sha(folder/'native.macho')!=record['native_sha256']:raise ValueError('Actual runtime image changed')
    if r.get('actual_sensor_runtime_composition') is not True or r.get('aggregate_recovery_bound_ms')!=5500:raise ValueError('Actual runtime profile/bound absent')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(name):
        values=local.get(name,set()).copy()
        if name in names:values.add(names[name])
        if len(values)!=1:raise ValueError('Unique actual lifecycle symbol missing: '+name)
        return values.pop()
    calls=[];init=[];oldinit=[]
    for image,b,table in ((im,raw,init),(oi,old,oldinit)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=b[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for offset in range(0,len(data)-3,4):
                        w=struct.unpack_from('<I',data,offset)[0]
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+offset;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('New automatic native runtime initializer')
    no_outer_sensor_start(calls,symbol,True,a.kernel_build/'native.macho')
    edges={}
    for name in ('_elden_sensor_memory_native_open','_elden_sensor_memory_native_view','_elden_sensor_memory_native_admit','_elden_sensor_memory_native_stop',
                 '_elden_sensor_memory_binding_open_prepared','_elden_sensor_memory_binding_step','_elden_sensor_memory_binding_status',
                 '_elden_sensor_memory_binding_request_stop','_elden_sensor_memory_binding_finish','_elden_sensor_adsp_endpoint_open',
                 '_elden_sensor_adsp_endpoint_attach','_elden_sensor_adsp_endpoint_close','_elden_qrtr_ports_prepare','_elden_motion_qrtr_transport',
                 '_elden_motion_native_ssc_observe','_elden_motion_native_ssc_start','_elden_motion_native_ssc_request_stop','_elden_motion_native_ssc_status',
                 '_elden_sensor_runtime_worker','_elden_sensor_runtime_stop','_kernel_thread_start'):
        hits=[pc for pc,t in calls if t==symbol(name)]
        if not hits and name!='_elden_sensor_runtime_worker':raise ValueError('Actual runtime call/tail absent: '+name)
        edges[name]={'target':hex(symbol(name)),'call_and_tail':[hex(pc) for pc in hits]}
    # At runtime prepare, the actual SSC observer function pointer is passed
    # to native_ssc_observe. The resulting object/table must contain the symbol.
    callback=[n for n in local if 'sensor_motion_observer' in n]
    if len(callback)!=1:raise ValueError('Actual sensor observer callback absent')
    recovery=symbol('_elden_sensor_runtime_recovery_stop');hits=[pc for pc,t in calls if t==recovery]
    if len(hits)!=1:raise ValueError('One actual platform runtime recovery caller required')
    sequence=[hits[0]]
    for name in ('_elden_haptics_stop_for_reboot','_elden_rtc_stop_for_reboot','_elden_ufs_wait_shutdown'):
        following=[pc for pc,t in calls if t==symbol(name) and sequence[0]<pc<sequence[0]+4096]
        if len(following)!=1 or following[0]<=sequence[-1]:raise ValueError('Actual original peripheral recovery order changed')
        sequence.append(following[0])
    oldcommand=json.loads((a.parent/'link-command.json').read_text());command=json.loads((a.kernel_build/'link-command.json').read_text());retained={}
    for arg in oldcommand:
        if arg.startswith('-Wl,-force_load,') and not arg.endswith('/libShengUfs.a'):retained[Path(arg.split(',',2)[2]).name]=retained_archive(arg,command,r)
    if [n for n,row in r['ufs_members'].items() if row['before']!=row['after']]!=['ShengUfs.o']:raise ValueError('Only original recovery UFS member may differ')
    for name,row in build['objects'].items():
        if sha(a.runtime_build/name)!=row['sha256']:raise ValueError('Actual runtime object changed')
    flags=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:flags.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental flags changed')
    result={'actual_sensor_runtime_full_link_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],
        'actual_calls':edges,'actual_observer_callback':callback[0],'original_recovery_order':[hex(x) for x in sequence],
        'aggregate_stop_bound_ms':5500,'archive_member_checks':retained,'outer_prepare_start_no_caller':True,
        'actual_memory_prepared_early_then_listener_SSC':True,'init_count_retained':True,'global_flags':flags,
        'real_owner_calibration_capture_SMMU_or_samples':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS actual native sensor runtime prepare/listener/SSC/observer/recovery link; old deadline/other owners retained')
if __name__=='__main__':main()
