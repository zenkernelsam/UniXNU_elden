"""Verify actual native camera DMA/capture/IOKit/PAR/4K linkage and scopes."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from camera_startup_audit import check_camera_startup
from peripheral_archive_audit import retained_archive
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','dma-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text());build=json.loads((a.dma_build/'result.json').read_text())
    for folder,report in ((a.kernel_build,r),(a.parent,parent)):
        if sha(folder/'kernel.macho')!=report['kernel_sha256'] or sha(folder/'native.macho')!=report['native_sha256']:raise ValueError('Actual camera DMA image differs')
    if r.get('actual_native_camera_dma_capture') is not True:raise ValueError('Actual camera DMA profile absent')
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    def symbol(n):
        v=local.get(n,set()).copy()
        if n in names:v.add(names[n])
        if len(v)!=1:raise ValueError('Missing unique actual DMA symbol: '+n)
        return v.pop()
    calls=[];initial=[];previous=[]
    for image,body,table in ((im,raw,initial),(oi,old,previous)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(data)-3,4):
                        word=struct.unpack_from('<I',data,off)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(initial)!=len(previous):raise ValueError('Automatic camera DMA initialization added')
    boundaries=check_camera_startup(calls,symbol,a.kernel_build/'native.macho',True,True,True)
    required=['_elden_camera_dma_view','_elden_camera_dma_admit','_elden_camera_dma_buffer','_elden_camera_dma_claim_frame','_elden_camera_dma_release_frame','_elden_camera_dma_stop',
        '_elden_camera_native_pipeline_queue','_elden_camera_native_pipeline_frame','_elden_camera_native_pipeline_service',
        '_elden_camera_board_prepare','_elden_camera_board_shutdown','_EldenWifiSmmu4KBuild','_EldenWifiSmmu4KAuditTables',
        '_EldenWifiSmmu4KBindTableMemory','_EldenWifiSmmu4KEncodeContext',
        '__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmm','__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv',
        '__ZN25IOGeneralMemoryDescriptor7prepareEj','__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj',
        '__ZN18IOMemoryDescriptor19createMappingInTaskEP4taskyjyy','__ZN11IOMemoryMap17getVirtualAddressEv']
    edges={}
    for n in required:
        hits=[pc for pc,target in calls if target==symbol(n)]
        if not hits:raise ValueError('Actual camera DMA/capture edge absent: '+n)
        edges[n]={'target':hex(symbol(n)),'calls_and_tails':[hex(pc) for pc in hits]}
    for caller,callee in [('_elden_camera_dma_capture_bind','_elden_camera_board_bind'),('_elden_camera_dma_capture_prepare','_elden_camera_board_prepare'),
        ('_elden_camera_dma_capture_queue_initial','_elden_camera_native_pipeline_queue'),('_elden_camera_dma_capture_frame','_elden_camera_dma_claim_frame'),
        ('_elden_camera_dma_capture_release','_elden_camera_dma_release_frame'),('_elden_camera_dma_capture_shutdown','_elden_camera_dma_stop')]:
        start=symbol(caller);end=min((v for v,n in starts if v>start),default=None)
        hits=[pc for pc,target in calls if target==symbol(callee)]
        if end is None or not any(start<=pc<end for pc in hits):raise ValueError('Actual capture wrapper call missing: '+caller)
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text());retained={}
    for value in oldcmd:
        path=Path(value.split(',')[-1])
        if value.startswith('-Wl,-force_load,'):retained[str(path)]=retained_archive(value,cmd,r)
        elif path.is_file() and path.suffix in ('.a','.o'):
            if cmd.count(value)!=1 or sha(path)!=r['inherited_inputs'].get(str(path)):raise ValueError('Original native input changed')
            retained[str(path)]=sha(path)
    for name,row in build['native_objects'].items():
        if sha(a.dma_build/name)!=row['sha256'] or cmd.count(str((a.dma_build/name).resolve()))!=1:raise ValueError('Actual DMA object differs/duplicate')
    asm=(a.dma_build/'NativeCameraDma.o-asm.log').read_text().lower()
    for op in ('s1e1r','s1e1w','par_el1','daif','civac','dsb'):
        if op not in asm:raise ValueError('Actual original PAR/cache instruction missing: '+op)
    imports=build['native_objects']['NativeCameraDma.o']['imports']
    for n in required[-6:]:
        if n not in imports:raise ValueError('Actual direct original IOKit import absent: '+n)
    base=symbol('__ZTV24IOBufferMemoryDescriptor')+16
    def u64(va):return struct.unpack_from('<Q',raw,backed_offset(im['segments'],va,8,len(raw)))[0]
    if u64(base+27*8)!=symbol('__ZN25IOGeneralMemoryDescriptor7prepareEj'):raise ValueError('Original prepare ABI changed')
    if u64(base+43*8)==symbol('__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv'):raise ValueError('Unqualified captured byte slot43 accepted')
    flags=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:flags.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental qualification changed')
    output={'passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'actual_call_edges':edges,
        'actual_startup_boundaries':boundaries,'all_original_archive_objects_retained':retained,'same_initializer_count':True,
        'actual_direct_Iokit_PAR_cache_4K':True,'original_prepare_slot':27,'captured_bytes43_refused':True,'global_flags':flags,
        'physical_earlySMMU_DMA_rails_frames_OS17camera':False,'hardware':False}
    a.output.write_text(json.dumps(output,indent=2)+'\n');print('PASS actual native DMA→cameraBoard/CCI/CAMSS edges, original directIOKit/PAR/cache/4K, all prior archives/recovery and no auto-start')
if __name__=='__main__':main()
