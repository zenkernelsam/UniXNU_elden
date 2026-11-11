/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Read-only PERF survey; firmware mailbox requests are written, levels are not. */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
#include "EldenCpuPerfResources.h"
#include "EldenScmiPerf.h"
#include "CpuPerfAccess.h"
extern "C" {
int elden_recovery_armed(void);
extern volatile int native_owner_data_status,elden_init_exec_accepted;
void elden_usb_log_putc(char);
__attribute__((weak)) volatile uint32_t sm8750_cpucp_owner;
}
namespace {
static_assert(scmi::kShmemBytes==ELDEN_CPUCP_SCMI_BYTES&&scmi::kDoorbell==ELDEN_CPUCP_DOORBELL,"SCMI layout differs from the verified board");
struct Window {IOMemoryMap *map;uintptr_t address;};
static Window tx,shared;
static scmi::Perf perf;
static unsigned attempted;
static bool malformed_access;
static uint64_t counter(void *) {uint64_t value;__asm__ volatile("mrs %0, cntvct_el0":"=r"(value));return value;}
static bool map(Window &w,uint64_t physical,size_t bytes) {
    IODeviceMemory *memory=IODeviceMemory::withRange(physical,bytes);
    w.map=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
    if(memory)memory->release();w.address=w.map?(uintptr_t)w.map->getVirtualAddress():0;return w.address!=0;
}
static void unmap(Window &w){if(w.map)w.map->release();w={};}
static uint32_t load(uintptr_t address){uint32_t value;__asm__ volatile("ldr %w0, [%1]\n\tdmb oshld":"=r"(value):"r"(address):"memory");return value;}
static void store(uintptr_t address,uint32_t value){__asm__ volatile("str %w0, [%1]\n\tdsb sy"::"r"(value),"r"(address):"memory");}
static uint32_t read(void *,uint32_t offset){
    if(offset>=ELDEN_CPUCP_SCMI_BYTES||(offset&3)){malformed_access=true;return 0;}
    return load(shared.address+offset);
}
static void write(void *,uint32_t offset,uint32_t value){
    if(offset>=ELDEN_CPUCP_SCMI_BYTES||(offset&3)){malformed_access=true;return;}
    store(shared.address+offset,value);
}
static void ring(void *,uint32_t value){store(tx.address+ELDEN_CPUCP_DOORBELL,value);}
static void pause(void *,uint64_t){IOSleep(1);}
static bool allowMessage(void *,uint32_t protocol,uint32_t message){return elden_cpu_perf_query(protocol,message);}
static void report(const char *format,...){
    char line[300];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    for(const char *p=line;*p;++p)elden_usb_log_putc(*p);
}
static bool show(const char *step,const scmi::Result &result){
    if(scmi::ok(result))return true;
    char why[64];scmi::describe(result,why,sizeof(why));
    report("\nELDEN_CPU_PERF stopped step=%s reason=%s error=%u status=%d transfers=%llu\n",
        step,why,(unsigned)result.error,result.status,perf.channel.transfers);return false;
}
static void worker(void *,wait_result_t){
    for(unsigned i=0;i<12000&&(!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted);++i)IOSleep(10);
    if(!elden_recovery_armed()||native_owner_data_status||!elden_init_exec_accepted){report("\nELDEN_CPU_PERF refused baseline gate\n");thread_terminate(current_thread());return;}
    uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&sm8750_cpucp_owner,&empty,0x45504350u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        report("\nELDEN_CPU_PERF refused CPUCP owner=%x\n",empty);thread_terminate(current_thread());return;
    }
    bool good=map(tx,ELDEN_CPUCP_TX_BASE,ELDEN_CPUCP_TX_BYTES)&&map(shared,ELDEN_CPUCP_SCMI_BASE,ELDEN_CPUCP_SCMI_BYTES);
    if(good){
        uint64_t hz;__asm__ volatile("mrs %0, cntfrq_el0":"=r"(hz));
        good=hz>=1000000&&hz<=100000000;
        if(good){
            perf.channel.io={nullptr,read,write,ring,counter,pause,nullptr,hz,30,allowMessage};
            perf.channel.sequence=scmi::headerToken(read(nullptr,scmi::kHeader));
            report("\nELDEN_CPU_PERF begin channel-status=%x hz=%llu; PERF_SET=0 LIMITS_SET=0\n",read(nullptr,scmi::kChannelStatus),hz);
            good=show("describe",scmi::probe(&perf));
            if(good){
                report("\nELDEN_CPU_PERF versions base=%x perf=%x domains=%u\n",perf.baseVersion,perf.perfVersion,perf.domains);
                for(unsigned d=0;d<scmi::kDomains;++d){
                    const scmi::Domain &domain=perf.domain[d];
                    report("\nELDEN_CPU_PERF domain=%u name=%s flags=%x levels=%u sustained-khz=%u perf=%u indexing=%u\n",
                        d,domain.name,domain.flags,domain.count,domain.sustainedKhz,domain.sustainedPerf,domain.indexing?1u:0u);
                    for(unsigned i=0;i<domain.count;++i)report("\nELDEN_CPU_LEVEL domain=%u index=%u raw=%u khz=%u latency-us=%u\n",
                        d,i,domain.levels[i].value,domain.levels[i].khz,domain.levels[i].latencyUs);
                    uint32_t high=0,low=0;scmi::Result limits=scmi::limitsGet(&perf,d,&high,&low);
                    report("\nELDEN_CPU_LIMITS domain=%u error=%u status=%d high-index=%u low-index=%u\n",d,(unsigned)limits.error,limits.status,high,low);
                    if(!scmi::ok(limits)&&limits.error!=scmi::kStatus){good=false;break;}
                }
                for(unsigned sample=0;sample<8&&good;++sample){
                    for(unsigned d=0;d<scmi::kDomains;++d){
                        uint32_t index=0,value=0;good=show("level-get",scmi::levelGet(&perf,d,&index,&value));
                        if(!good)break;
                        const scmi::Domain &domain=perf.domain[d];
                        report("\nELDEN_CPU_CURRENT sample=%u domain=%u raw=%u index=%u table-khz=%u exact=%u\n",
                            sample,d,value,index,domain.levels[index].khz,domain.levels[index].value==value?1u:0u);
                    }
                    if(good&&sample!=7)IOSleep(3000);
                }
            }
        }
    }else report("\nELDEN_CPU_PERF refused mapping\n");
    bool free=shared.address&&(read(nullptr,scmi::kChannelStatus)&scmi::kChannelFree);
    report("\nELDEN_CPU_PERF end ok=%u channel-free=%u access-error=%u transfers=%llu; levels unchanged\n",
        good?1u:0u,free?1u:0u,malformed_access?1u:0u,perf.channel.transfers);
    if(!perf.channel.transfers||free){unmap(shared);unmap(tx);__atomic_store_n(&sm8750_cpucp_owner,0,__ATOMIC_RELEASE);}
    /* An unresolved transfer retains its mapping/lease; no forced free/retry. */
    thread_terminate(current_thread());
}
}
extern "C" void elden_cpu_perf_survey_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-cpu-survey",&mode,sizeof(mode));
    if(mode!=1||__atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else report("\nELDEN_CPU_PERF refused worker allocation\n");
}
