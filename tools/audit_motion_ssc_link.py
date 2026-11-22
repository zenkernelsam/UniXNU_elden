"""Audit actual final SSC worker/parser/HID/recovery edges and retained owners."""
from pathlib import Path
import argparse,bisect,hashlib,json,re,struct,subprocess,sys
from replay_xnu_objects import sections
from peripheral_archive_audit import retained_archive
from peripheral_startup_audit import no_outer_sensor_start
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','parent','ssc-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    report=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=report['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=report['native_sha256'] or sha(a.parent/'kernel.macho')!=prior['kernel_sha256']:raise ValueError('Image identity differs')
    im=macho(raw);oi=macho(old);ni=macho(native);names={n:v for v,g in exports(raw,im).items() for n in g};symbols={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:symbols.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        v=symbols.get(n,set()).copy()
        if n in names:v.add(names[n])
        if len(v)!=1:raise ValueError('Unique actual SSC symbol missing: '+n)
        return v.pop()
    def read(blob,image,va,n):
        at=backed_offset(image['segments'],va,n,len(blob));return blob[at:at+n]
    calls=[];initial=[];oldinitial=[]
    for image,blob,table in [(im,raw,initial),(oi,old,oldinitial)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                b=blob[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(b)//8),b))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(b)-3,4):
                        word=struct.unpack_from('<I',b,off)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2),word&0xfc000000==0x94000000))
    if len(initial)!=len(oldinitial):raise ValueError('Automatic SSC initialization added')
    start=symbol('_elden_motion_native_ssc_start')
    runtime=report.get('actual_sensor_runtime_composition') is True
    if runtime:no_outer_sensor_start([(pc,target) for pc,target,link in calls],symbol,True,a.kernel_build/'native.macho')
    elif any(target==start for pc,target,link in calls):raise ValueError('Unqualified SSC auto-start caller')
    edges={}
    for n in ('_elden_ssc_init','_elden_ssc_server','_elden_ssc_step','_elden_ssc_receive','_elden_ssc_stop','_elden_ssc_demand',
              '_elden_ssc_attribute_report','_elden_ssc_report_error','_elden_ssc_spec_valid','_elden_ssc_pick_rate',
              '_elden_ssc_discovery_request','_elden_ssc_attribute_request','_elden_ssc_sampling_request',
              '_elden_motion_client_response','_elden_motion_discovery_report','_elden_motion_samples',
              '_elden_motion_native_hid_start','_elden_motion_native_hid_bind','_elden_motion_native_hid_packet','_elden_motion_native_hid_stop',
              '_kernel_thread_start','_assert_wait_timeout','_thread_block','_clear_wait','_clock_timebase_info'):
        target=symbol(n);hits=[hex(pc) for pc,t,link in calls if t==target]
        if not hits:raise ValueError('Actual SSC call/tail missing: '+n)
        # Newly linked callable code must survive prelink unchanged; data
        # addresses are relocated separately and remain bound by exports.
        if n.startswith('_elden_') and read(native,ni,target,32)!=read(raw,im,target,32):raise ValueError('Prelink changed actual native method: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    recovery=symbol('_elden_sensor_runtime_recovery_stop' if runtime else '_elden_motion_native_ssc_recovery_stop');recovery_hits=[pc for pc,t,link in calls if t==recovery and link]
    if len(recovery_hits)!=1:raise ValueError('One actual SSC recovery call required')
    sequence=[recovery_hits[0]]
    for n in ('_elden_haptics_stop_for_reboot','_elden_rtc_stop_for_reboot','_elden_ufs_wait_shutdown'):
        hits=[pc for pc,t,link in calls if t==symbol(n) and link and sequence[0]<pc<sequence[0]+4096]
        if len(hits)!=1 or hits[0]<=sequence[-1]:raise ValueError('Actual motion/haptics/RTC/UFS order differs')
        sequence.append(hits[0])
    changes=[n for n,row in report['ufs_members'].items() if row['before']!=row['after']]
    if changes!=['ShengUfs.o']:raise ValueError('Other UFS owner changed')
    # The packaged J317 orientation consumer ships as an independent default-off
    # object: its four public entries must be exported code with no initializer,
    # no call/tail from an unrelated caller (orientation/classifier internals
    # are allowed), and no data reference anywhere outside __LINKEDIT.
    orientation=['_elden_motion_consumer_begin','_elden_motion_consumer_sample',
                 '_elden_motion_consumer_stop','_elden_motion_consumer_status']
    if any(n not in names for n in orientation):raise ValueError('Orientation entry absent from export table')
    entries=sorted(symbol(n) for n in orientation);entry_set=set(entries)
    if any(v in initial for v in entries):raise ValueError('Orientation entry initializes automatically')
    text_addrs=sorted({v for group in symbols.values() for v in group})
    def next_symbol(v):
        at=bisect.bisect_right(text_addrs,v)
        return text_addrs[at] if at<len(text_addrs) else 1<<64
    internal=[*entries,symbol('_elden_motion_rotation')]
    spans=[(v,next_symbol(v)) for v in internal]
    foreign=[(pc,t) for pc,t,link in calls if t in entry_set and not any(lo<=pc<hi for lo,hi in spans)]
    if foreign:raise ValueError('Unrelated caller reaches orientation entry: '+hex(foreign[0][0]))
    for seg in im['segments']:
        if seg['name']=='__LINKEDIT':continue
        for sec in seg['sections']:
            body=raw[sec['fileoff']:sec['fileoff']+sec['size']]
            for v in entries:
                if body.find(struct.pack('<Q',v))!=-1:raise ValueError('Orientation entry referenced by '+seg['name']+'/'+sec['name'])
    before_cmd=json.loads((a.parent/'link-command.json').read_text());after_cmd=json.loads((a.kernel_build/'link-command.json').read_text())
    retained={}
    for v in before_cmd:
        if v.startswith('-Wl,-force_load,') and not v.endswith('/libShengUfs.a'):
            retained[Path(v.split(',',2)[2]).name]=retained_archive(v,after_cmd,report)
    for name in ('NativeSsc.o','Ssc.o','SscCodec.o','SscWire.o','SscPb.o'):
        if any(n.endswith('/__mod_init_func') for n in sections(a.ssc_build/name)):raise ValueError('Unexpected automatic SSC initializer')
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental flags changed')
    result={'ssc_component_link_passed':True,'kernel_sha256':report['kernel_sha256'],'native_sha256':report['native_sha256'],
            'actual_calls':edges,'recovery_order_calls':[hex(n) for n in sequence],'no_SSC_auto_start':True,'init_count_preserved':len(initial),
            'orientation_exports':{n:hex(names[n]) for n in orientation},
            'orientation_not_automatically_initialized':True,'orientation_not_called_or_referenced':True,
            'unchanged_peripheral_archives_retained':all(r['unchanged_path'] for r in retained.values()),'actual_retained_archive_checks':retained,'only_changed_UFS_member':changes,'legacy_flags':flags,
            'shared_QRTR_board_power_mount_clock_provider':False,'sensor_PD_registry_file_service':False,'real_sensor_samples_or_UI_rotation':False,
            'provider_bound':False,'native_J317_or_UI_connected':False,'physical_samples':False,'boot_packaged':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual SSC worker/strict parser/HID calls, no auto-start, motion→haptics→RTC→UFS recovery and existing archives retained')
if __name__=='__main__':main()
