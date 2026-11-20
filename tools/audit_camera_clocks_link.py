"""Check actual Gen4 native camera clock/board edges and immutable parent inputs."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from camera_startup_audit import check_camera_startup
from peripheral_archive_audit import retained_archive
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','clock-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    report=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text());built=json.loads((a.clock_build/'result.json').read_text())
    for folder,r in ((a.kernel_build,report),(a.parent,parent)):
        if sha(folder/'kernel.macho')!=r['kernel_sha256'] or sha(folder/'native.macho')!=r['native_sha256']:raise ValueError('Actual kernel/native changed')
    if not report.get('actual_native_camera_clock_board'):raise ValueError('Actual native camera graph profile absent')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('Unique actual camera clock symbol missing: '+n)
        return values.pop()
    calls=[];initial=[];previous=[]
    for image,body,table in ((im,raw,initial),(oi,old,previous)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for offset in range(0,len(data)-3,4):
                        word=struct.unpack_from('<I',data,offset)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+offset;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(initial)!=len(previous):raise ValueError('Automatic camera clock initializer added')
    startup=check_camera_startup(calls,symbol,a.kernel_build/'native.macho',True,True,report.get('actual_native_camera_dma_capture') is True);edges={}
    for n in ('_elden_camera_clocks_open','_elden_camera_clock_resource','_elden_camera_domain_resource','_elden_camera_pins_prepare',
        '_elden_camera_sensor_reset','_elden_camera_clock_observe','_elden_camera_clock_observe_cleanup','_elden_camera_clocks_stop',
        '_elden_camera_native_clock','_elden_camera_native_domain','_elden_camera_native_sensor_reset','_elden_camera_native_clock_observe_cleanup',
        '_elden_camera_powered_capture_stop','_elden_camera_power_session_stop','_elden_camera_native_clocks_stop'):
        hits=[pc for pc,target in calls if target==symbol(n)]
        if not hits:raise ValueError('Actual native camera clock/board edge missing: '+n)
        edges[n]={'target':hex(symbol(n)),'calls_and_tails':[hex(pc) for pc in hits]}
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text());retained={}
    for value in oldcmd:
        path=Path(value.split(',')[-1])
        if value.startswith('-Wl,-force_load,'):retained[str(path)]=retained_archive(value,cmd,report)
        elif path.is_file() and path.suffix in ('.a','.o'):
            if cmd.count(value)!=1 or sha(path)!=report['inherited_inputs'].get(str(path)):raise ValueError('Original native object changed')
            retained[str(path)]=sha(path)
    for n,row in built['native_objects'].items():
        if cmd.count(str((a.clock_build/n).resolve()))!=1 or sha(a.clock_build/n)!=row['sha256']:raise ValueError('Clock native object missing/duplicated/changed')
    nativeasm=(a.clock_build/'NativeClocks.o-asm.log').read_text().lower()
    for token in ('ldr','str','dsb','cntpct_el0'):
        if token not in nativeasm:raise ValueError('Actual MMIO/time/barrier missing')
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental qualification changed')
    output={'passed':True,'kernel_sha256':report['kernel_sha256'],'native_sha256':report['native_sha256'],'actual_clock_board_edges':edges,
        'actual_startup_boundaries':startup,'retained_original_archive_objects':retained,'original_initializer_count':True,
        'new_native_map_barrier_scalar_time':True,'global_flags':flags,'physical_PLL_RPMh_pins_DMA_owner_frames_OSservice':False,'hardware':False}
    a.output.write_text(json.dumps(output,indent=2)+'\n');print('PASS actual native camera39clock graph/board/MMIO edges, exact startup scopes, all original inputs/recovery and no auto-start')
if __name__=='__main__':main()
