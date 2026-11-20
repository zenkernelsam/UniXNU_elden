"""Check actual final shared ADSP/GLINK close and sensor RPC endpoint calls."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','parent','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    report=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=report['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=report['native_sha256'] or sha(a.parent/'kernel.macho')!=parent['kernel_sha256']:raise ValueError('Final transport image identity differs')
    im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('Unique actual endpoint symbol absent: '+n)
        return values.pop()
    calls=[];init=[];oldinit=[]
    for image,blob,table in ((im,raw,init),(oi,old,oldinit)):
        for seg in image['segments']:
            for sec in seg['sections']:
                b=blob[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(b)//8),b))
                if image is im and sec['name']=='__text':
                    for offset in range(0,len(b)-3,4):
                        word=struct.unpack_from('<I',b,offset)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+offset;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('Automatic endpoint initializer added')
    runtime=report.get('actual_sensor_runtime_composition') is True
    from peripheral_startup_audit import no_outer_sensor_start
    no_outer_sensor_start(calls,symbol,runtime,a.kernel_build/'native.macho')
    edges={}
    for n in ('_elden_adsp_channel_epoch','_elden_adsp_channel_status','_elden_adsp_channel_close','_sheng_adsp_send_epoch',
              '_elden_glink_close_request','_elden_glink_close_status','_qcom_glink_send','_qcom_glink_stop',
              '_elden_sensor_memory_binding_receive','_elden_sensor_memory_binding_channel','_assert_wait_timeout','_clear_wait','_thread_block'):
        target=symbol(n);hits=[hex(pc) for pc,t in calls if t==target]
        if not hits:raise ValueError('Actual shared-owner/endpoint edge absent: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    archive_changes={}
    qrtr=report.get('single_original_ADSP_IPCRTR_QRTR_owner') is True
    expected={'libEldenAdspRuntime.a':{'AdspProbe.o','ShengQrtr.o','QcomQrtr.o'} if qrtr else {'AdspProbe.o'},'libEldenPmicTransport.a':{'QcomGlink.o'}}
    declarations=report.get('cumulative_transport_archives') if qrtr else report['changed_transport_archives']
    if not isinstance(declarations,dict):raise ValueError('Cumulative original transport proof absent')
    for name,allowed in expected.items():
        changed=[n for n,r in declarations[name].items() if r['before']!=r['after']]
        if set(changed)!=allowed:raise ValueError('Other original transport members changed')
        archive_changes[name]=changed
    oldcommand=json.loads((a.parent/'link-command.json').read_text());command=json.loads((a.kernel_build/'link-command.json').read_text())
    from peripheral_archive_audit import retained_archive
    actual_archive_checks={}
    for v in oldcommand:
        if v.startswith('-Wl,-force_load,'):actual_archive_checks[Path(v.split(',',2)[2]).name]=retained_archive(v,command,report)
    # The inherited ADSP authentication path uses actual OS17 IOBuffer prepare
    # slot27 and bytes slot41 (the separate prior Actuator slot43 was append).
    def u64(va):return struct.unpack_from('<Q',raw,backed_offset(im['segments'],va,8,len(raw)))[0]
    base=symbol('__ZTV24IOBufferMemoryDescriptor')+16
    original_slots={27:'__ZN25IOGeneralMemoryDescriptor7prepareEj',41:'__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv',43:'__ZN24IOBufferMemoryDescriptor11appendBytesEPKvm'}
    for slot,n in original_slots.items():
        if u64(base+slot*8)!=symbol(n):raise ValueError('Inherited ADSP original IOBuffer ABI differs')
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Experimental global flags changed')
    result={'actual_shared_ADSP_GLlNK_endpoint_link_passed':True,'kernel_sha256':report['kernel_sha256'],'native_sha256':report['native_sha256'],
            'actual_direct_call_edges':edges,'only_original_type_consumer_members_changed':archive_changes,'original_IOBuffer_slots':original_slots,
            'original_other_archive_inputs_and_init_count_retained':True,'no_sensor_endpoint_auto_start':True,
            'physical_DSP_DMA_retirement_or_capture_SMMU_provider':False,'hardware':False,'global_flags':flags}
    result['actual_original_archive_member_checks']=actual_archive_checks
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual original ADSP/GLINK close→sensor endpoint/RPC calls, two type consumers only, original IOBuffer ABI/other owners retained')
if __name__=='__main__':main()
