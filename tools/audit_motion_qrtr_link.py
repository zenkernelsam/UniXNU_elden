"""Audit actual existing IPCRTR/SSC queue, provider, ABI and retained members."""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess, sys
from peripheral_archive_audit import retained_archive, members
from peripheral_startup_audit import no_outer_sensor_start
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text())
    for folder,report in ((a.kernel_build,r),(a.parent,prior)):
        if sha(folder/'kernel.macho')!=report['kernel_sha256'] or sha(folder/'native.macho')!=report['native_sha256']:raise ValueError('Qualified image changed')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();image=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,image).items() for n in g};local={};symbols=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));symbols.append((int(m[1],16),m[2]))
    def symbol(name):
        v=local.get(name,set()).copy()
        if name in names:v.add(names[name])
        if len(v)!=1:raise ValueError('Unique actual QRTR symbol absent: '+name)
        return v.pop()
    calls=[];init=[];previous=[]
    for im,body,table in ((image,raw,init),(oi,old,previous)):
        for seg in im['segments']:
            for sec in seg['sections']:
                block=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(block)//8),block))
                if im is image and sec['name']=='__text':
                    for offset in range(0,len(block)-3,4):
                        word=struct.unpack_from('<I',block,offset)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+offset;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(init)!=len(previous):raise ValueError('New automatic QRTR/sensor initializer')
    runtime=r.get('actual_sensor_runtime_composition') is True
    no_outer_sensor_start(calls,symbol,runtime,a.kernel_build/'native.macho')
    if not runtime:
        for name in ('_elden_qrtr_ports_prepare','_elden_motion_qrtr_transport','_elden_motion_native_ssc_start'):
            if any(target==symbol(name) for pc,target in calls):raise ValueError('Unqualified owner/SSC auto-start: '+name)
    edges={}
    for name in ('_elden_qrtr_ports_rx_config','_elden_qrtr_ports_bind','_elden_qrtr_ports_link','_elden_qrtr_ports_snapshot',
                 '_elden_qrtr_ports_receive','_elden_qrtr_original_service','_elden_qrtr_service','_elden_qrtr_port_open',
                 '_elden_qrtr_port_level','_elden_qrtr_port_send','_elden_qrtr_port_withdraw','_elden_adsp_channel_epoch',
                 '_elden_adsp_channel_status','_sheng_adsp_send_epoch','_qcom_qrtr_frame','_qcom_qrtr_control_frame',
                 '_qcom_qrtr_receive','_qcom_qrtr_watch','_qcom_qrtr_forget_remote'):
        hits=[pc for pc,target in calls if target==symbol(name)]
        if not hits:raise ValueError('Actual QRTR/provider edge missing: '+name)
        edges[name]={'target':hex(symbol(name)),'call_and_tail':[hex(pc) for pc in hits]}
    # The concrete SSC transport function stores real original callbacks in
    # the compiled native object; host tests execute each through that vtable.
    provider=['_elden_motion_qrtr_transport','_elden_qrtr_ports_owner','_elden_qrtr_ports_admit']
    for name in provider:symbol(name)
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text())
    retained={}
    for arg in oldcmd:
        if arg.startswith('-Wl,-force_load,'):retained[Path(arg.split(',',2)[2]).name]=retained_archive(arg,cmd,r)
    adsp=retained['libEldenAdspRuntime.a']
    if adsp.get('unchanged_path'):
        # A later peripheral image can retain the already-qualified QRTR
        # archive. Verify its actual cumulative member hashes rather than
        # requiring the same type-layout replacement again.
        original_arg=next(v for v in oldcmd if v.endswith('/libEldenAdspRuntime.a'))
        actual=members(Path(original_arg.split(',',2)[2]));proof=r.get('cumulative_transport_archives',{}).get('libEldenAdspRuntime.a')
        if not isinstance(proof,dict) or set(actual)!=set(proof) or any(actual[n]!=proof[n]['after'] for n in actual):raise ValueError('Qualified retained QRTR archive evidence differs')
    else:
        changed=adsp['changed_members']
        if set(changed)!={'AdspProbe.o','ShengQrtr.o','QcomQrtr.o'}:raise ValueError('Unexpected QRTR layout consumer')
    # Keep the original ADSP metadata call targets, not captured guesses.
    base=symbol('__ZTV24IOBufferMemoryDescriptor')+16
    slots={27:'__ZN25IOGeneralMemoryDescriptor7prepareEj',41:'__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv',43:'__ZN24IOBufferMemoryDescriptor11appendBytesEPKvm'}
    for number,name in slots.items():
        off=backed_offset(image['segments'],base+number*8,8,len(raw))
        if struct.unpack_from('<Q',raw,off)[0]!=symbol(name):raise ValueError('Original ADSP IOBuffer ABI changed')
    flags=json.loads((a.kernel_build/'kernel.json').read_text())
    flags={name:flags.get(name) for name in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(value is not False for value in flags.values()):raise ValueError('Global qualification flags changed')
    result={'actual_shared_QRTR_SSC_component_link_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],
            'actual_calls':edges,'actual_archive_member_checks':retained,'original_IOBuffer_slots':slots,
            'initializer_count_unchanged':True,'no_owner_transport_SSC_auto_start':True,'global_flags':flags,
            'real_owner_receive_intents_or_remote_retirement_installed':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS actual sole QRTR/SSC provider/queue/flow edges, exact3members, original ABI/other components retained')
if __name__=='__main__':main()
