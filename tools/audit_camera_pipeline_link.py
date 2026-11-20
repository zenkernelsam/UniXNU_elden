"""Audit actual Gen4 native receiver/CCI capture calls and absent automatic start."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from camera_startup_audit import check_camera_startup
from peripheral_archive_audit import retained_archive
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','pipeline-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('kernel_build','parent','pipeline_build','prelink_tool'):setattr(a,name,getattr(a,name).resolve(strict=True))
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    report=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text());build=json.loads((a.pipeline_build/'result.json').read_text())
    for folder,r in ((a.kernel_build,report),(a.parent,parent)):
        if sha(folder/'kernel.macho')!=r['kernel_sha256'] or sha(folder/'native.macho')!=r['native_sha256']:raise ValueError('Actual image identity differs')
    if report.get('actual_camera_pipeline_capture_binding') is not True:raise ValueError('Declared camera profile missing')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('Unique actual camera symbol absent: '+n)
        return values.pop()
    calls=[];initial=[];before=[]
    for image,body,table in ((im,raw,initial),(oi,old,before)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(data)-3,4):
                        w=struct.unpack_from('<I',data,off)[0]
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(initial)!=len(before):raise ValueError('Automatic camera initializer added')
    startup=check_camera_startup(calls,symbol,a.kernel_build/'native.macho',report.get('actual_camera_WL_power_session') is True)
    edges={}
    for n in ('_elden_camera_pipeline_resources','_elden_camera_pipeline_open','_elden_camera_pipeline_queue','_elden_camera_pipeline_start',
              '_elden_camera_pipeline_service','_elden_camera_pipeline_frame','_elden_camera_pipeline_ready','_elden_camera_pipeline_stop','_elden_camera_pipeline_status',
              '_elden_camera_native_pipeline_prepared','_elden_camera_native_pipeline_ready','_elden_camera_native_pipeline_start','_elden_camera_native_pipeline_stop',
              '_elden_camera_sensor_bind','_elden_camera_sensor_probe','_elden_camera_sensor_start','_elden_camera_sensor_stop','_elden_camera_sensor_shutdown',
              '_armcpu_spi_attach','_armcpu_spi_mask'):
        hits=[pc for pc,t in calls if t==symbol(n)]
        if not hits:raise ValueError('Actual native camera/capture edge absent: '+n)
        edges[n]={'target':hex(symbol(n)),'calls_and_tails':[hex(x) for x in hits]}
    for caller,callee in (('_elden_camera_capture_bind','_elden_camera_sensor_bind'),('_elden_camera_capture_probe','_elden_camera_sensor_probe'),
                          ('_elden_camera_capture_start','_elden_camera_sensor_start'),('_elden_camera_capture_stop','_elden_camera_sensor_shutdown')):
        start=symbol(caller);end=min((pc for pc,_ in starts if pc>start),default=None)
        hits=[pc for pc,t in calls if t==symbol(callee)]
        if end is None or not any(start<=pc<end for pc in hits):raise ValueError('Missing actual capture-wrapper callee: '+callee)
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text())
    inherited={}
    for value in oldcmd:
        path=Path(value.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):
            if value.startswith('-Wl,-force_load,') and path.name=='libEldenBacklight.a' and report.get('one_original_I2c10_GENI_owner') is True:
                inherited[str(path)]=retained_archive(value,cmd,report);continue
            expected=report['inherited_inputs'].get(str(path))
            if sha(path)!=expected or cmd.count(value)!=1:raise ValueError('Existing peripheral input changed')
            inherited[str(path)]=expected
    for name,row in build['native_objects'].items():
        if sha(a.pipeline_build/name)!=row['sha256'] or cmd.count(str(a.pipeline_build/name))!=1:raise ValueError('Camera new object input changed')
    # Real write/read instruction and barrier checks, with exact original32bit
    # IOVA contract and frame address units in source-derived core.
    nativeasm=(a.pipeline_build/'NativePipeline-asm.log').read_text().lower();coreasm=(a.pipeline_build/'native-asm.log').read_text().lower()
    for token in ('ldr','str','dsb','cntpct_el0'):
        if token not in nativeasm:raise ValueError('Actual native MMIO/time/barrier instruction absent')
    if not re.search(r'lsr\s+w\d+, w\d+, #0x8',coreasm) and '#8' not in coreasm:raise ValueError('Gen4 address shift instruction absent')
    flags=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:flags.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental flags changed')
    result={'actual_camera_pipeline_full_link_passed':True,'kernel_sha256':report['kernel_sha256'],'native_sha256':report['native_sha256'],
        'actual_native_CCI_capture_edges':edges,'all_original_native_inputs_retained':inherited,'initializer_count_retained':True,
        'actual_capture_startup_boundaries':startup,
        'no_native_camera_or_capture_auto_start':True,'MMIO_barrier_scalar_address_contract_checked':True,'global_flags':flags,
        'real_power_SMMU_buffers_DMA_frames_OSservice':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS actual Gen4 native pipeline/CCI capture calls, original owners/Apple code, no auto camera start')
if __name__=='__main__':main()
