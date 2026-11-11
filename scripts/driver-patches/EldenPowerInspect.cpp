// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only SMEM / charging-DSP survey. Source parsing reuses QcomSmem from
// UniXNU-sheng. It runs on a local snapshot: no allocation or indices in the
// hardware SMEM are written, no DSP/PMIC reset or charger command is issued.
#include "EldenPowerResources.h"
extern "C" {
#include "QcomSmem.h"
}
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <stdarg.h>

extern "C" {
extern volatile int native_owner_data_status,elden_init_exec_accepted;
int elden_recovery_armed(void);
void elden_usb_log_putc(char);
}
namespace {
static unsigned attempted;
static void report(const char *format,...) {
    char line[300];va_list args;va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);va_end(args);
    for(const char *p=line;*p;++p)elden_usb_log_putc(*p);
}
__attribute__((noinline)) static uint32_t read32(uintptr_t address) {
    uint32_t value;__asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(address):"memory");return value;
}
static void item(const QcomSmem &snapshot,const QcomSmemPartition &part,unsigned id) {
    uint32_t offset=0,bytes=0;int rc=qcom_smem_find(&snapshot,&part,(uint16_t)id,&offset,&bytes);
    if(rc || (offset&3) || offset>snapshot.size || bytes>snapshot.size-offset) {
        report("\nELDEN_POWER_INSPECT item=%u rc=%d bytes=%u\n",id,rc,bytes);return;
    }
    report("\nELDEN_POWER_INSPECT item=%u rc=0 offset=%08x bytes=%u\n",id,offset,bytes);
    // Only public layout/state words, never FIFO payloads or other SMEM items.
    if(id==ELDEN_POWER_SMP2P_IN && bytes>=20) {
        const uint32_t magic=qcom_smem_read32(&snapshot,offset),version=qcom_smem_read32(&snapshot,offset+4);
        const uint32_t pids=qcom_smem_read32(&snapshot,offset+8),counts=qcom_smem_read32(&snapshot,offset+12);
        const unsigned total=counts&0xffff,valid=counts>>16;
        report("\nELDEN_POWER_INSPECT smp2p magic=%08x version=%08x pids=%08x counts=%08x flags=%08x\n",
            magic,version,pids,counts,qcom_smem_read32(&snapshot,offset+16));
        if(magic!=0x504d5324u || (version&255)!=1 || pids!=ELDEN_POWER_ADSP_PID ||
           total>16 || valid>total || 20u+valid*20u>bytes)return;
        for(unsigned n=0;n<valid;++n) {
            char name[17];for(unsigned j=0;j<4;++j) {
                uint32_t value=qcom_smem_read32(&snapshot,offset+20+n*20+j*4);
                for(unsigned k=0;k<4;++k)name[j*4+k]=(char)(value>>(k*8));
            }
            name[16]=0;
            if(strcmp(name,"slave-kernel"))continue;
            const uint32_t state=qcom_smem_read32(&snapshot,offset+20+n*20+16);
            report("\nELDEN_POWER_INSPECT adsp slave-kernel=%08x fatal=%u ready=%u handover=%u\n",
                state,state&1,(state>>1)&1,(state>>2)&1);
        }
    }
    if(id==ELDEN_POWER_GLINK_DESC && bytes>=16) {
        report("\nELDEN_POWER_INSPECT glink-desc words=%08x,%08x,%08x,%08x\n",
            qcom_smem_read32(&snapshot,offset),qcom_smem_read32(&snapshot,offset+4),
            qcom_smem_read32(&snapshot,offset+8),qcom_smem_read32(&snapshot,offset+12));
    }
}
static void inspect() {
    report("\nELDEN_POWER_INSPECT begin writes=0 charger-commands=0 dsp-reset=0\n");
    IODeviceMemory *memory=IODeviceMemory::withRange(ELDEN_POWER_SMEM_BASE,ELDEN_POWER_SMEM_SIZE);
    IOMemoryMap *map=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
    if(memory)memory->release();
    uint32_t *copy=(map && map->getVirtualAddress())?static_cast<uint32_t *>(IOMalloc(ELDEN_POWER_SMEM_SIZE)):nullptr;
    if(!copy){if(map)map->release();report("\nELDEN_POWER_INSPECT refused mapping/snapshot\n");return;}
    const uintptr_t base=(uintptr_t)map->getVirtualAddress();
    for(unsigned offset=0;offset<ELDEN_POWER_SMEM_SIZE;offset+=4) {
        copy[offset/4]=read32(base+offset);
        if(offset && !(offset&0xffff))IOSleep(1);
    }
    map->release(); // All decoding below uses ordinary owned RAM.
    QcomSmem snapshot={reinterpret_cast<volatile uint8_t *>(copy),ELDEN_POWER_SMEM_SIZE};
    const uint16_t hosts[]={ELDEN_POWER_ADSP_PID,QCOM_SMEM_HOST_GLOBAL};
    const unsigned ids[]={ELDEN_POWER_SMP2P_IN,ELDEN_POWER_SMP2P_OUT,ELDEN_POWER_GLINK_DESC,ELDEN_POWER_GLINK_TX,ELDEN_POWER_GLINK_RX};
    for(unsigned n=0;n<2;++n) {
        QcomSmemPartition part={};
        const uint16_t local=n?QCOM_SMEM_HOST_GLOBAL:QCOM_SMEM_HOST_APPS;
        const int rc=qcom_smem_partition(&snapshot,local,hosts[n],&part);
        report("\nELDEN_POWER_INSPECT partition=%s rc=%d offset=%08x bytes=%u cacheline=%u\n",
            n?"global":"apps-adsp",rc,part.offset,part.size,part.cacheline);
        if(rc)continue;
        // qcom_smem_find expects a valid immutable partition. Reject any
        // allocator boundary or alignment outside this snapshot before calling it.
        const uint32_t up=qcom_smem_read32(&snapshot,part.offset+12),down=qcom_smem_read32(&snapshot,part.offset+16);
        if(up<32 || up>down || down>part.size || part.cacheline>part.size)continue;
        for(unsigned id:ids)item(snapshot,part,id);
    }
    IOFree(copy,ELDEN_POWER_SMEM_SIZE);
    report("\nELDEN_POWER_INSPECT end; battery values require qualified PMIC GLINK client\n");
}
static void worker(void *,wait_result_t) {
    for(unsigned n=0;n<12000 && (!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted);++n)IOSleep(10);
    if(elden_recovery_armed() && native_owner_data_status==0 && elden_init_exec_accepted)inspect();
    else report("\nELDEN_POWER_INSPECT refused baseline not ready\n");
    thread_terminate(current_thread());
}
}
extern "C" void elden_power_inspect_start(void) {
    unsigned enabled=0;PE_parse_boot_argn("elden-power-inspect",&enabled,sizeof(enabled));
    if(enabled!=1 || __atomic_exchange_n(&attempted,1,__ATOMIC_ACQ_REL))return;
    thread_t thread=nullptr;
    if(kernel_thread_start(worker,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);
    else report("\nELDEN_POWER_INSPECT refused worker allocation\n");
}
