"""Prepare an opt-in, recovery-gated single framebuffer mapping candidate."""
from pathlib import Path
import hashlib,json,zipfile
r=Path(__file__).resolve().parents[1];src=r/'research/basic-bundle-20261108/source-compiled-v91';p=r/'research/fbwc-20261111';p.mkdir(exist_ok=True);assert not (p/'objects.json').exists();port=Path('D:/Desktop/Git/UniXNU_elden')
sha=lambda b:hashlib.sha256(b).hexdigest()
files={n:(port/'drivers/EldenFramebuffer'/n).read_bytes() for n in ('WcPolicy.h','MappingCheck.c','TestWcPolicy.c')}
console=(r/'research/display-alias-review-20261111/video_console.c').read_text()
anchor='void\ninitialize_screen(PE_Video * boot_vinfo, unsigned int op)\n{';assert console.count(anchor)==1
console=console.replace(anchor,'''static unsigned elden_fb_console_suppressed,elden_fb_console_maps;
uint64_t elden_fb_console_state(void)
{
    return __atomic_load_n(&elden_fb_console_suppressed,__ATOMIC_ACQUIRE) |
        (__atomic_load_n(&elden_fb_console_maps,__ATOMIC_ACQUIRE)?2u:0u);
}
'''+anchor+'''
    unsigned fbwc=0;PE_parse_boot_argn("fbwc",&fbwc,sizeof(fbwc));
    if(fbwc==1){
        __atomic_store_n(&elden_fb_console_suppressed,1u,__ATOMIC_RELEASE);
        disableConsoleOutput=TRUE; /* Existing disabled-console paths still mirror USB. */
        return; /* No early or later text-console alias of the framebuffer. */
    }
''')
anchor='\t\t\t\tnewVideoVirt = ml_io_map_unmappable';assert console.count(anchor)==1
console=console.replace(anchor,'''                if(new_vinfo.v_physaddr<UINT64_C(0xfde15000) && newMapSize &&
                    (new_vinfo.v_physaddr>=UINT64_C(0xfc800000) || newMapSize>UINT64_C(0xfc800000)-new_vinfo.v_physaddr))
                    __atomic_fetch_add(&elden_fb_console_maps,1u,__ATOMIC_RELEASE);
'''+anchor)
files['video_console.c']=console.encode()
guard=(src/'cpu/SmpGuard.inc').read_text();oldguard=guard
anchor='static unsigned gEldenSmpCount;';assert guard.count(anchor)==1
guard=guard.replace(anchor,'''static volatile unsigned gEldenFramebufferReady;
extern "C" bool elden_smp_framebuffer_ready(void)
{
  return __atomic_load_n(&gEldenFramebufferReady,__ATOMIC_ACQUIRE) &&
    __atomic_load_n(&gEldenSmpOwned,__ATOMIC_ACQUIRE) &&
    !__atomic_load_n(&gEldenSmpPanic,__ATOMIC_ACQUIRE);
}
'''+anchor)
anchor='  if(rc){thread_terminate(current_thread());return;}\n  const uint64_t start=eldenDeadlineCounter();';assert guard.count(anchor)==1
guard=guard.replace(anchor,'  if(rc){thread_terminate(current_thread());return;}\n  __atomic_store_n(&gEldenFramebufferReady,1u,__ATOMIC_RELEASE);\n  const uint64_t start=eldenDeadlineCounter();')
files['SmpGuard.inc']=guard.encode();hp=(src/'cpu/HoldPolicy.h').read_text()
recovery=(src/'usb/EldenSdamRecovery.inc').read_text();expanded=oldguard.replace('#include "HoldPolicy.h"',hp);assert recovery.count(expanded)==1
files['EldenSdamRecovery.inc']=recovery.replace(expanded,guard.replace('#include "HoldPolicy.h"',hp)).encode()
glue=(src/'tree/scripts/driver-patches/EldenDisplayGlue.cpp').read_text()
anchor='static IOService *activeDisplay,*activeNub;';assert glue.count(anchor)==1
glue=glue.replace(anchor,'''#include "WcPolicy.h"
extern "C" unsigned long gPhysBase,gPhysSize;
extern "C" uint64_t elden_fb_console_state(void);
extern "C" bool elden_smp_framebuffer_ready(void);
extern "C" int elden_fb_check_mapping(uint64_t,uint64_t,uint64_t *,uint64_t *,uint64_t *);
static unsigned fbWcRequested(void){unsigned value=0;PE_parse_boot_argn("fbwc",&value,sizeof(value));return value;}
'''+anchor)
anchor='extern "C" {\nvoid native_legacy_display_probe(void) {';assert glue.count(anchor)==1
helpers='''extern "C" unsigned native_legacy_fb_cache_mode(void)
{
    unsigned mode=fbWcRequested();if(!mode)return 1;if(mode!=1)return 0;
    PE_Video v={};if(PE_current_console(&v)||!elden_smp_framebuffer_ready())return 0;
    bool valid=elden_fb_wc_layout(v.v_baseAddr,v.v_rowBytes*v.v_height,v.v_width,v.v_height,v.v_rowBytes,v.v_depth,v.v_rotate,gPhysBase,gPhysSize,elden_fb_console_state());
    report("\\nELDEN_FBWC policy=%u console=%llu physbase=%llx physsize=%llx\\n",valid,(unsigned long long)elden_fb_console_state(),(unsigned long long)gPhysBase,(unsigned long long)gPhysSize);
    return valid?2:0;
}
extern "C" { static void startDisplay(void); }
static void fbWcWorker(void *,wait_result_t)
{
    for(unsigned n=0;n<480&&!elden_smp_framebuffer_ready();n++)IOSleep(250);
    if(!elden_smp_framebuffer_ready()){report("\\nELDEN_FBWC timeout before mapping; no framebuffer access\\n");thread_terminate(current_thread());return;}
    startDisplay();thread_terminate(current_thread());
}
extern "C" void native_legacy_display_late_probe(void)
{
    if(!fbWcRequested()){startDisplay();return;}
    static unsigned queued;if(!requested||stage||!__sync_bool_compare_and_swap(&queued,0,1))return;
    thread_t worker=nullptr;
    if(kernel_thread_start(fbWcWorker,nullptr,&worker)!=KERN_SUCCESS){report("\\nELDEN_FBWC worker creation failed\\n");return;}
    thread_deallocate(worker);
}
'''
glue=glue.replace(anchor,helpers+anchor)
glue=glue.replace('void native_legacy_display_late_probe(void) {','static void startDisplay(void) {',1)
anchor='    IODeviceMemory *memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);';assert glue.count(anchor)==1
glue=glue.replace(anchor,'''    IOMemoryDescriptor *memory=nullptr;
    if(fbWcRequested()){
        IOAddressRange range={video.v_baseAddr,video.v_rowBytes*video.v_height};
        if(native_legacy_fb_cache_mode()==2)
            memory=IOMemoryDescriptor::withOptions(&range,1,0,nullptr,kIOMemoryTypePhysical64|kIODirectionInOut|(kIOWriteCombineCache<<kIOMemoryBufferCacheShift));
    }else memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);''')
anchor='    if(good)qualifiedMapping=mapping;';assert glue.count(anchor)==1
glue=glue.replace(anchor,'''    if(good&&fbWcRequested()){
        uint64_t page=0,par=0,mair=0;int checked=6;
        if(elden_smp_framebuffer_ready() && native_legacy_fb_cache_mode()==2 &&
           (mapping->getMapOptions()&kIOMapCacheMask)==kIOMapWriteCombineCache)
            checked=elden_fb_check_mapping(mapping->getVirtualAddress(),mapping->getLength(),&page,&par,&mair);
        report("\\nELDEN_FBWC mapping=%d page=%llx par=%llx mair=%llx\\n",checked,(unsigned long long)page,(unsigned long long)par,(unsigned long long)mair);
        good=checked==0;
    }
'''+anchor)
anchor='    if(!qualifiedMapping || !elden_recovery_armed() || ml_at_interrupt_context() || !ml_get_interrupts_enabled())return false;';assert glue.count(anchor)==1
glue=glue.replace(anchor,anchor+'\n    if(fbWcRequested()&&!elden_smp_framebuffer_ready())return false;')
files['EldenDisplayGlue.cpp']=glue.encode()
native=(src/'tree/build/native-legacy-display/src/NativeLegacyDisplay.cpp').read_text()
native='extern "C" unsigned native_legacy_fb_cache_mode(void);\n'+native
anchor='        IOMemoryMap *mapping=provider->mapDeviceMemoryWithIndex(0,kIOMapInhibitCache);';assert native.count(anchor)==1
native=native.replace(anchor,'''        unsigned cache=native_legacy_fb_cache_mode();if(!cache)return false;
        IOMemoryMap *mapping=provider->mapDeviceMemoryWithIndex(0,cache==2?kIOMapWriteCombineCache:kIOMapInhibitCache);''')
files['NativeLegacyDisplay.cpp']=native.encode()
for n,b in files.items():(p/n).write_bytes(b)
manifest={'files':{n:sha(b) for n,b in files.items()},'parent_source_manifest_sha256':sha((src/'manifest.json').read_bytes()),'parent_console_sha256':sha((r/'research/display-alias-review-20261111/video_console.c').read_bytes()),'scope':'experimental fbwc=1: no console alias; deferred display after confirmed hardware watchdog; fixed isolated framebuffer; WC descriptor; all-page AT check before pixel access','hardware_tested':False}
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'source_files':len(files),'no_device_access':True},indent=2))
