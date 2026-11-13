// SPDX-License-Identifier: BSD-2-Clause-Patent
// Elden GOP lifecycle, adapted from UniXNU-sheng LegacyDisplayProbe.cpp.
// Only the firmware framebuffer is used; no borrowed panel/GPU register writes.
#include "OriginalLegacyFramebufferABI.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSSymbol.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <mach/kmod.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <stdarg.h>

extern "C" {
extern kmod_info_t g_kernel_kmod_info;
void native_legacy_display_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
IOService *native_legacy_display_allocate(void);
kern_return_t OSKextLoadKextWithIdentifier(const char *);
int elden_recovery_armed(void);
void elden_usb_log_putc(char);
int elden_usb_preview_line(const char *,unsigned);
void elden_user_wait_start(void);
#if defined(ELDEN_WIFI_FW_CHECK_BUILD) && ELDEN_WIFI_FW_CHECK_BUILD
void elden_wifi_fw_check_start(void);
#endif
void elden_keyevents_start(void);
#ifdef ELDEN_HID_BUILD
void elden_hid_input_start(void);
#endif
#ifdef ELDEN_TOUCH_INSPECT_BUILD
void elden_touch_inspect_start(void);
#endif
#ifdef ELDEN_TOUCH_RUNTIME_BUILD
void elden_touch_runtime_start(void);
#endif
#ifdef ELDEN_DATA_INSPECT_BUILD
void elden_data_inspect_start(void);
#endif
#ifdef ELDEN_BLUETOOTH_BUILD
void elden_bluetooth_inspect_start(void);
#endif
#ifdef ELDEN_WCN_POWER_BUILD
void elden_wcn_power_start(void);
#endif
#ifdef ELDEN_POWER_INSPECT_BUILD
void elden_power_inspect_start(void);
#endif
}

#include "WcPolicy.h"
extern "C" unsigned long gPhysBase,gPhysSize;
extern "C" uint64_t elden_fb_console_state(void);
extern "C" bool elden_smp_framebuffer_ready(void);
extern "C" int elden_fb_check_mapping(uint64_t,uint64_t,uint64_t *,uint64_t *,uint64_t *);
static unsigned fbWcRequested(void){unsigned value=0;PE_parse_boot_argn("fbwc",&value,sizeof(value));return value;}
static IOService *activeDisplay,*activeNub;
static IOMemoryMap *qualifiedMapping; // Borrowed while activeDisplay owns it.
static unsigned stage,frames,transfers,retirements,validations,clients;
static bool requested,consoleReleased,hasPresented;
static bool previewFullFrame;
static bool previewNonBlank;
static unsigned previewCaptured,previewAutomatic,previewAfterInput;
extern "C" void elden_preview_after_input(void){__atomic_store_n(&previewAfterInput,5,__ATOMIC_RELEASE);}
extern "C" bool native_legacy_frame_diagnostics_wanted(void) {
    // Full-pixel scans serve only the existing bounded preview requests.
    return previewCaptured<12 && (previewAutomatic<2 ||
        __atomic_load_n(&previewAfterInput,__ATOMIC_ACQUIRE)>0);
}
static void report(const char *format,...) {
    char text[512];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    for(const char *p=text;*p;++p)elden_usb_log_putc(*p);
}
// Adapted from sheng LegacyDisplayProbe::busyReport. Observe only, with
// bounded traversal and output; no busy-counter adjustment or matching request.
static void iokitBusyWorker(void *,wait_result_t) {
    const unsigned delays[]={10,20,30,60,120,60};
    unsigned elapsed=0;
    for(unsigned round=0;round<6;round++) {
        IOSleep(delays[round]*1000);elapsed+=delays[round];
        IOService *root=IOService::getServiceRoot();
        unsigned busy=root?root->getBusyState():0,visited=0,shown=0,providers=0;
        bool complete=true;
        IORegistryIterator *it=IORegistryIterator::iterateOver(gIOServicePlane,kIORegistryIterateRecursively);
        if(!it){report("\nELDEN_IOKIT t=%u root=%u iterator=0\n",elapsed,busy);continue;}
        while(OSObject *object=it->getNextObject()) {
            if(++visited>512){complete=false;break;}
            IOService *service=OSDynamicCast(IOService,object);if(!service)continue;
            if(service->metaCast("IONetworkStack"))providers|=1;
            if(service->metaCast("IOAESAccelerator"))providers|=2;
            if(service->metaCast("IO80211Interface"))providers|=4;
            if(service->metaCast("IOEthernetInterface"))providers|=8;
            unsigned count=service->getBusyState();if(!count)continue;
            bool busyChild=false;unsigned children=0;
            if(OSIterator *ch=service->getChildIterator(gIOServicePlane)) {
                while(OSObject *child=ch->getNextObject()) {
                    if(++children>64){busyChild=true;complete=false;break;}
                    IOService *c=OSDynamicCast(IOService,child);
                    if(c&&c->getBusyState()){busyChild=true;break;}
                }
                ch->release();
            }
            if(!busyChild&&shown++<8)
                report("\nELDEN_IOKIT leaf t=%u id=%llx name=%.64s class=%.64s busy=%u state=%x\n",
                    elapsed,service->getRegistryEntryID(),service->getName(),service->getMetaClass()->getClassName(),count,(unsigned)service->getState());
        }
        complete=complete&&it->isValid();it->release();
        report("\nELDEN_IOKIT t=%u root-present=%u busy=%u visited=%u leaves=%u complete=%u providers=%x\n",
            elapsed,root!=nullptr,busy,visited,shown,complete,providers);
    }
    thread_terminate(current_thread());
}

struct Preview {
    unsigned id,sourceFrame;
    char session[24];
    uint16_t pixels[238*380];
    uint16_t statusBar[238*96];
};
static uint64_t previewCounter(){uint64_t n;__asm__ volatile("mrs %0,cntpct_el0":"=r"(n));return n;}
static void previewWorker(void *argument,wait_result_t) {
    Preview *preview=static_cast<Preview *>(argument);
    uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    const uint64_t began=previewCounter();
    auto send=[&](const char *text)->bool {
        while(previewCounter()-began<hz*15ULL) {
            if(elden_usb_preview_line(text,(unsigned)strlen(text)))return true;
            IOSleep(2);
        }
        return false;
    };
    char line[1100];
    snprintf(line,sizeof(line),"\nELDEN_FRAME_BEGIN session=%s id=%u source=%u width=238 height=380 format=rgb565le\n",preview->session,preview->id,preview->sourceFrame);
    bool ok=hz==19200000 && send(line);uint32_t crc=0xffffffffU;
    static const char hex[]="0123456789abcdef";
    for(unsigned y=0;y<380 && ok;++y) {
        int n=snprintf(line,sizeof(line),"\nELDEN_FRAME_ROW id=%u y=%u hex=",preview->id,y);
        const uint8_t *bytes=reinterpret_cast<const uint8_t *>(&preview->pixels[y*238]);
        for(unsigned i=0;i<476;++i) {
            const uint8_t b=bytes[i];line[n++]=hex[b>>4];line[n++]=hex[b&15];
            crc^=b;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1)));
        }
        line[n++]='\n';line[n]=0;ok=send(line);
    }
    if(ok) {
        snprintf(line,sizeof(line),"\nELDEN_FRAME_END id=%u crc32=%08x bytes=180880\n",preview->id,crc^0xffffffffU);
        ok=send(line);
    }
    if(!ok)report("\nELDEN_FRAME_ABORT id=%u\n",preview->id);
    if(ok) {
        // Native-scale upper-right crop: resolve battery text without changing
        // the display or widening the existing 1100-byte transport line limit.
        snprintf(line,sizeof(line),"\nELDEN_BAR_BEGIN session=%s id=%u source=%u x=1666 y=0 width=238 height=96 format=rgb565le\n",
            preview->session,preview->id,preview->sourceFrame);
        bool barOk=send(line);crc=0xffffffffU;
        for(unsigned y=0;y<96&&barOk;++y){
            int n=snprintf(line,sizeof(line),"\nELDEN_BAR_ROW id=%u y=%u hex=",preview->id,y);
            const uint8_t *bytes=reinterpret_cast<const uint8_t *>(&preview->statusBar[y*238]);
            for(unsigned i=0;i<476;++i){
                const uint8_t b=bytes[i];line[n++]=hex[b>>4];line[n++]=hex[b&15];
                crc^=b;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1)));
            }
            line[n++]='\n';line[n]=0;barOk=send(line);
        }
        if(barOk){snprintf(line,sizeof(line),"\nELDEN_BAR_END id=%u crc32=%08x bytes=45696\n",preview->id,crc^0xffffffffU);barOk=send(line);}
        if(!barOk)report("\nELDEN_BAR_ABORT id=%u\n",preview->id);
    }
    IOFree(preview,sizeof(Preview));thread_terminate(current_thread());
}
static void capturePreview() {
    unsigned enabled=0;PE_parse_boot_argn("elden-frame-preview",&enabled,sizeof(enabled));
    unsigned afterInput=__atomic_load_n(&previewAfterInput,__ATOMIC_ACQUIRE);
    if(enabled!=1 || !previewNonBlank || previewCaptured>=12 ||
       (!afterInput && (!previewFullFrame || previewAutomatic>=2)) || !qualifiedMapping ||
       qualifiedMapping->getLength()!=23152640 || ml_at_interrupt_context() || !ml_get_interrupts_enabled())return;
    Preview *preview=static_cast<Preview *>(IOMalloc(sizeof(Preview)));
    if(!preview)return;
    if(afterInput)__atomic_fetch_sub(&previewAfterInput,1,__ATOMIC_ACQ_REL);else ++previewAutomatic;
    preview->id=++previewCaptured;preview->sourceFrame=transfers;
    bzero(preview->session,sizeof(preview->session));
    PE_parse_boot_argn("elden-debug-session",preview->session,sizeof(preview->session));
    if(strlen(preview->session)!=16){IOFree(preview,sizeof(Preview));return;}
    // Read only the mapped, already presented GOP buffer; nearest-neighbour 1:8.
    const uintptr_t base=qualifiedMapping->getVirtualAddress();
    __asm__ volatile("dsb sy":::"memory");
    for(unsigned y=0;y<380;++y)for(unsigned x=0;x<238;++x) {
        uint32_t pixel=*reinterpret_cast<volatile const uint32_t *>(base+(y*8)*7616+(x*8)*4);
        preview->pixels[y*238+x]=(uint16_t)(((pixel>>19)&31)<<11 | ((pixel>>10)&63)<<5 | ((pixel>>3)&31));
    }
    static_assert(1666+238==1904&&96<=3040,"Crop must stay within the qualified GOP geometry");
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<238;++x){
        uint32_t pixel=*reinterpret_cast<volatile const uint32_t *>(base+y*7616+(1666+x)*4);
        preview->statusBar[y*238+x]=(uint16_t)(((pixel>>19)&31)<<11 | ((pixel>>10)&63)<<5 | ((pixel>>3)&31));
    }
    thread_t worker=nullptr;
    if(kernel_thread_start(previewWorker,preview,&worker)==KERN_SUCCESS)thread_deallocate(worker);
    else IOFree(preview,sizeof(Preview));
}
static bool videoQualified(PE_Video &video) {
    int status=PE_current_console(&video);
    report("\nELDEN_DISPLAY video status=%d base=%llx width=%llu height=%llu stride=%llu depth=%llu rotate=%u\n",status,
        (uint64_t)video.v_baseAddr,(uint64_t)video.v_width,(uint64_t)video.v_height,(uint64_t)video.v_rowBytes,(uint64_t)video.v_depth,(unsigned)video.v_rotate);
    return !status && video.v_baseAddr==0xfc800000ULL &&
        video.v_width==1904 && video.v_height==3040 && video.v_rowBytes==7616 &&
        video.v_depth==32 && video.v_rotate==0;
}
static bool originalClassQualified() {
    const OSSymbol *name=OSSymbol::withCString("IOMobileFramebufferLegacy");
    const OSMetaClass *meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;
    OSSafeReleaseNULL(name);
    report("\nELDEN_DISPLAY original meta=%p binding=%p size=%u\n",meta,&IOMobileFramebufferLegacy::gMetaClass,meta?meta->getClassSize():0);
    if(!meta || meta!=&IOMobileFramebufferLegacy::gMetaClass || meta->getClassSize()!=0x18a0)return false;
    const char *chain[]={"IOMobileFramebufferLegacy","IOMobileFramebufferService","IOMobileFramebuffer","IOService"};
    for(const char *name:chain) {
        report("\nELDEN_DISPLAY ancestry expected=%s actual=%s\n",name,meta?meta->getClassName():"none");
        if(!meta || strcmp(meta->getClassName(),name))return false;
        meta=meta->getSuperClass();
    }
    return true;
}

extern "C" unsigned native_legacy_fb_cache_mode(void)
{
    unsigned mode=fbWcRequested();if(!mode)return 1;if(mode!=1)return 0;
    PE_Video v={};if(PE_current_console(&v)||!elden_smp_framebuffer_ready())return 0;
    bool valid=elden_fb_wc_layout(v.v_baseAddr,v.v_rowBytes*v.v_height,v.v_width,v.v_height,v.v_rowBytes,v.v_depth,v.v_rotate,gPhysBase,gPhysSize,elden_fb_console_state());
    report("\nELDEN_FBWC policy=%u console=%llu physbase=%llx physsize=%llx\n",valid,(unsigned long long)elden_fb_console_state(),(unsigned long long)gPhysBase,(unsigned long long)gPhysSize);
    return valid?2:0;
}
extern "C" { static void startDisplay(void); }
static void fbWcWorker(void *,wait_result_t)
{
    for(unsigned n=0;n<480&&!elden_smp_framebuffer_ready();n++)IOSleep(250);
    if(!elden_smp_framebuffer_ready()){report("\nELDEN_FBWC timeout before mapping; no framebuffer access\n");thread_terminate(current_thread());return;}
    startDisplay();thread_terminate(current_thread());
}
extern "C" void native_legacy_display_late_probe(void)
{
    if(!fbWcRequested()){startDisplay();return;}
    static unsigned queued;if(!requested||stage||!__sync_bool_compare_and_swap(&queued,0,1))return;
    thread_t worker=nullptr;
    if(kernel_thread_start(fbWcWorker,nullptr,&worker)!=KERN_SUCCESS){report("\nELDEN_FBWC worker creation failed\n");return;}
    thread_deallocate(worker);
}
extern "C" {
void native_legacy_display_probe(void) {
    // IOBSD publication and the SDAM worker must precede all new original ABI calls.
    requested=true;report("\nELDEN_DISPLAY deferred to IOBSD\n");
}
static void startDisplay(void) {
    if(!requested || stage)return;
    stage=1;
    for(unsigned n=0;n<3000 && !elden_recovery_armed();++n)IOSleep(10);
    if(!elden_recovery_armed()){report("\nELDEN_DISPLAY refused: recovery not armed\n");return;}
    PE_Video video={};
    bool videoOK=videoQualified(video),metaOK=originalClassQualified();
    if(!videoOK || !metaOK) {
        report("\nELDEN_DISPLAY refused: GOP geometry or original metaclass\n");return;
    }
    stage=2;
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){report("\nELDEN_DISPLAY preModLoad failed\n");return;}
    native_legacy_display_static_init();
    auto metaStatus=OSMetaClass::postModLoad(handle);
    report("\nELDEN_DISPLAY metaclass status=%d original-size=0x18a0 ancestry=verified\n",metaStatus);
    if(metaStatus)return;
    auto surfaceStatus=OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface");
    OSDictionary *matching=IOService::serviceMatching("IOSurfaceRoot");
    IOService *surface=matching?IOService::waitForMatchingService(matching,5000000000ULL):nullptr;
    bool surfaceReady=surfaceStatus==0 && surface;
    report("\nELDEN_DISPLAY IOSurface status=%d found=%u\n",surfaceStatus,surface!=nullptr);
    OSSafeReleaseNULL(surface);OSSafeReleaseNULL(matching);
    if(!surfaceReady)return;
    stage=3;
    IOService *display=native_legacy_display_allocate();
    if(!display)return;
    if(!display->metaCast("IOMobileFramebufferLegacy") || !display->metaCast("IOMobileFramebuffer") ||
       !display->metaCast("IOService") || !display->init()) {
        report("\nELDEN_DISPLAY init failed\n");display->release();return;
    }
    IOService *nub=new IOService,*parent=IOService::getResourceService();
    IORegistryEntry *dtParent=IORegistryEntry::fromPath("/arm-io",gIODTPlane);
    IOMemoryDescriptor *memory=nullptr;
    if(fbWcRequested()){
        IOAddressRange range={video.v_baseAddr,video.v_rowBytes*video.v_height};
        if(native_legacy_fb_cache_mode()==2)
            memory=IOMemoryDescriptor::withOptions(&range,1,0,nullptr,kIOMemoryTypePhysical64|kIODirectionInOut|(kIOWriteCombineCache<<kIOMemoryBufferCacheShift));
    }else memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);
    OSArray *ranges=OSArray::withCapacity(1);
    bool nubAttached=false,nubStarted=false,dtAttached=false,attached=false,started=false;
    if(nub && parent && dtParent && memory && ranges && nub->init() && ranges->setObject(memory)) {
        nub->setDeviceMemory(ranges);nub->setName("NativeGOPFramebuffer");
        nubAttached=nub->attach(parent);if(nubAttached)nubStarted=nub->start(parent);
        const char type[]="display-subsystem";OSData *data=OSData::withBytes(type,sizeof(type));
        if(nubStarted && data && nub->setProperty("device_type",data)) {
            nub->setName("gop-display",gIODTPlane);dtAttached=nub->attachToParent(dtParent,gIODTPlane);
        }
        OSSafeReleaseNULL(data);
        SInt32 score=0;
        if(dtAttached && display->probe(nub,&score)==display)attached=display->attach(nub);
        if(attached){stage=4;activeDisplay=display;started=display->start(nub);}
    }
    report("\nELDEN_DISPLAY start=%u nub=%u dt=%u attached=%u stage=%u\n",started,nubStarted,dtAttached,attached,stage);
    OSSafeReleaseNULL(ranges);OSSafeReleaseNULL(memory);
    if(started){
        activeNub=nub;stage=5;OSSafeReleaseNULL(dtParent);
        thread_t busyWorker=nullptr;
        if(kernel_thread_start(iokitBusyWorker,nullptr,&busyWorker)==KERN_SUCCESS)thread_deallocate(busyWorker);
        else report("\nELDEN_IOKIT worker-start-failed\n");
        elden_user_wait_start();elden_keyevents_start();
#if defined(ELDEN_WIFI_FW_CHECK_BUILD) && ELDEN_WIFI_FW_CHECK_BUILD
        elden_wifi_fw_check_start();
#endif
#ifdef ELDEN_HID_BUILD
        elden_hid_input_start();
#endif
#ifdef ELDEN_TOUCH_INSPECT_BUILD
        elden_touch_inspect_start();
#endif
#ifdef ELDEN_TOUCH_RUNTIME_BUILD
        elden_touch_runtime_start();
#endif
#ifdef ELDEN_DATA_INSPECT_BUILD
        elden_data_inspect_start();
#endif
#ifdef ELDEN_BLUETOOTH_BUILD
        elden_bluetooth_inspect_start();
#endif
#ifdef ELDEN_WCN_POWER_BUILD
        elden_wcn_power_start();
#endif
#ifdef ELDEN_POWER_INSPECT_BUILD
        elden_power_inspect_start();
#endif
        return;
    }
    activeDisplay=nullptr;qualifiedMapping=nullptr;
    if(attached)display->detach(nub);
    if(dtAttached)nub->detachFromParent(dtParent,gIODTPlane);
    if(nubStarted)nub->stop(parent);if(nubAttached)nub->detach(parent);
    OSSafeReleaseNULL(dtParent);OSSafeReleaseNULL(nub);display->release();
}

bool native_legacy_gop_mapping_observed(IOService *display,IOMemoryMap *mapping,uint32_t width,uint32_t height) {
    PE_Video video={};
    bool good=display==activeDisplay && mapping && videoQualified(video) &&
        width==video.v_width && height==video.v_height && mapping->getVirtualAddress() &&
        mapping->getPhysicalAddress()==video.v_baseAddr && mapping->getLength()>=video.v_rowBytes*video.v_height;
    if(good&&fbWcRequested()){
        uint64_t page=0,par=0,mair=0;int checked=6;
        if(elden_smp_framebuffer_ready() && native_legacy_fb_cache_mode()==2 &&
           (mapping->getMapOptions()&kIOMapCacheMask)==kIOMapWriteCombineCache)
            checked=elden_fb_check_mapping(mapping->getVirtualAddress(),mapping->getLength(),&page,&par,&mair);
        report("\nELDEN_FBWC mapping=%d page=%llx par=%llx mair=%llx\n",checked,(unsigned long long)page,(unsigned long long)par,(unsigned long long)mair);
        good=checked==0;
    }
    if(good)qualifiedMapping=mapping;
    report("\nELDEN_DISPLAY mapping=%u pa=%llx bytes=%llu width=%u height=%u\n",good,
        mapping?(uint64_t)mapping->getPhysicalAddress():0,mapping?(uint64_t)mapping->getLength():0,width,height);
    return good;
}
bool native_legacy_prepare_scanout(void) {
    if(!qualifiedMapping || !elden_recovery_armed() || ml_at_interrupt_context() || !ml_get_interrupts_enabled())return false;
    if(fbWcRequested()&&!elden_smp_framebuffer_ready())return false;
    if(!consoleReleased) {
        // The normal console release preserves PE_state.video. serial_console's
        // disabled branches still copy every character to the USB log ring.
        initialize_screen(nullptr,kPEReleaseScreen);consoleReleased=true;
        report("\nELDEN_DISPLAY console released; USB logging retained\n");
    }
    return true;
}
void native_legacy_registration_observed(IOService *display) {
    report("\nELDEN_DISPLAY registered class=%s\n",display->getMetaClass()->getClassName());
    OSSerialize *xml=OSSerialize::withCapacity(4096);
    if(xml && display->serializeProperties(xml)) {
        report("\nELDEN_DISPLAY properties bytes=%u\n",xml->getLength());
        for(unsigned i=0;i<xml->getLength() && i<8192;++i)if(xml->text()[i])elden_usb_log_putc(xml->text()[i]);
        elden_usb_log_putc('\n');
    }
    OSSafeReleaseNULL(xml);
}
void native_legacy_client_enter(task_t,uint32_t type) {
    if(++clients<=20)report("\nELDEN_DISPLAY client enter type=%u\n",type);
}
void native_legacy_client_observed(IOReturn status,IOUserClient *client) {
    if(clients<=20)report("\nELDEN_DISPLAY client status=%x class=%s\n",status,client?client->getMetaClass()->getClassName():"none");
}
void native_legacy_pm_observed(unsigned event,IOReturn status,unsigned long ordinal) {
    report("\nELDEN_DISPLAY PM event=%u status=%x ordinal=%lu\n",event,status,ordinal);
}
void native_legacy_swap_stage(unsigned event) {(void)event;}
void native_legacy_swap_validation_observed(bool accepted,unsigned reason,const void *,const void *) {
    if(++validations<=32 || (!accepted && validations%128==0))
        report("\nELDEN_DISPLAY swap=%u accepted=%u reason=%u\n",validations,accepted,reason);
}
void native_legacy_surface_rejected(uint64_t w,uint64_t h,uint32_t format,uint32_t planes,uint64_t stride,uint64_t allocation,uint64_t bytes) {
    if(validations<=32)report("\nELDEN_DISPLAY rejected surface=%llux%llu format=%x planes=%u stride=%llu allocation=%llu bytes=%llu\n",w,h,format,planes,stride,allocation,bytes);
}
void native_legacy_frame_observed(unsigned kind,uint64_t count,uint64_t minx,uint64_t miny,uint64_t maxx,uint64_t maxy) {
    previewNonBlank=kind==0 && count>0;
    // Include dark lock screens with status/clock content near the edges.
    // The central firmware boot logo alone is not a useful GUI preview.
    previewFullFrame=previewNonBlank && (count>1904ULL*3040/2 || minx<300 || miny<600 || maxy>2400);
    if(++frames<=32)report("\nELDEN_DISPLAY frame=%u kind=%u pixels=%llu bounds=%llu,%llu-%llu,%llu\n",frames,kind,count,minx,miny,maxx,maxy);
}
void native_legacy_frame_colors_observed(const uint32_t *colors,const uint64_t *counts,uint64_t other) {
    if(frames<=8)report("\nELDEN_DISPLAY colors=%x/%llu,%x/%llu other=%llu\n",colors[0],counts[0],colors[1],counts[1],other);
}
void native_legacy_transfer_observed(IOReturn status,const void *,uint64_t bytes) {
    if(++transfers<=32 || status)report("\nELDEN_DISPLAY transfer=%u status=%x bytes=%llu\n",transfers,status,bytes);
    if(status==kIOReturnSuccess){hasPresented=true;capturePreview();}
    else if(consoleReleased && !hasPresented) {
        initialize_screen(nullptr,kPEAcquireScreen);consoleReleased=false;
        report("\nELDEN_DISPLAY first transfer failed; text console restored\n");
    }
}
void native_legacy_copy_timing_observed(uint64_t ticks,uint64_t bytes) {
    if(transfers<32)report("\nELDEN_DISPLAY CPU copy bytes=%llu ticks=%llu\n",bytes,ticks);
}
void native_legacy_retirement_observed(IOService *) {
    if(++retirements<=32)report("\nELDEN_DISPLAY retired=%u\n",retirements);
}
bool native_legacy_gpu_offer(void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t) {return false;}
void native_legacy_gpu_fallback_observed(int status) {report("\nELDEN_DISPLAY unexpected GPU fallback=%d\n",status);}
void native_legacy_vsync_observed(unsigned event,unsigned hz) {
    static unsigned counts[6];
    if(event<6 && ++counts[event]<=3)report("\nELDEN_DISPLAY vsync event=%u hz=%u\n",event,hz);
}
__attribute__((noreturn)) void native_legacy_backend_guard(unsigned slot,void *,void *caller) {
    panic("ELDEN_DISPLAY unsupported backend slot=%u caller=%p stage=%u frames=%u transfers=%u",slot,caller,stage,frames,transfers);
}
}
