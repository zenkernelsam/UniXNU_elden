/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Elden board client of the canonical sheng ADSP transport. Explicit opt-in
 * telemetry, no charger/UCSI SET or fabricated IOPMPowerSource readings.
 * A future validated board publisher consumes these raw coherent snapshots.
 */
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include "ShengAdsp.h"
#include "ChargePoll.h"
#include <stdarg.h>
extern "C" {
bool elden_recovery_armed(void);
extern volatile int native_owner_data_status, elden_init_exec_accepted;
void elden_usb_log_putc(char);
volatile uint32_t elden_pmic_stats[12];
volatile uint32_t elden_pmic_raw[EldenChargeFieldCount];
volatile uint64_t elden_pmic_valid;
volatile uint32_t elden_pmic_sequence;
}
namespace {
struct Incoming {uint32_t epoch, size;uint8_t bytes[SHENG_ADSP_MAX_MESSAGE];};
static Incoming inbox[8];
static unsigned head, used, attempted;
static IOLock *lock;
static volatile unsigned linkUp, linkEpoch, overflow;
static ShengAdspChannel *channel;
static void report(const char *format,...) {
    char text[320];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    for(const char *p=text;*p;++p)elden_usb_log_putc(*p);
}
static void state(void *,int value) {
    const unsigned up=value!=0;
    IOLockLock(lock);
    if (__atomic_exchange_n(&linkUp,up,__ATOMIC_ACQ_REL)!=up)
        __atomic_add_fetch(&linkEpoch,1,__ATOMIC_RELEASE);
    IOLockUnlock(lock);
    thread_wakeup((event_t)&linkEpoch);
}
static void receive(void *,const uint8_t *data,uint32_t size) {
    IOLockLock(lock);
    if(size>SHENG_ADSP_MAX_MESSAGE || used==8) __atomic_store_n(&overflow,1,__ATOMIC_RELEASE);
    else {
        Incoming &m=inbox[(head+used)%8];m.epoch=__atomic_load_n(&linkEpoch,__ATOMIC_ACQUIRE);
        m.size=size;memcpy(m.bytes,data,size);++used;
    }
    IOLockUnlock(lock);thread_wakeup((event_t)&linkEpoch);
}
static void publishRaw(const EldenChargePoll &poll) {
    __atomic_add_fetch(&elden_pmic_sequence,1u,__ATOMIC_ACQ_REL);
    __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
    for(unsigned i=0;i<EldenChargeFieldCount;++i)elden_pmic_raw[i]=poll.committed.value[i].raw;
    __atomic_store_n(&elden_pmic_valid,poll.committed.valid,__ATOMIC_RELEASE);
    __atomic_add_fetch(&elden_pmic_sequence,1u,__ATOMIC_RELEASE);
    elden_pmic_stats[2]=poll.cycles;elden_pmic_stats[3]=poll.responses;
    elden_pmic_stats[4]=poll.errors;elden_pmic_stats[5]=poll.timeouts;
    elden_pmic_stats[6]=poll.notifications;elden_pmic_stats[7]=poll.ignored;
    report("\nELDEN_PMIC cycle=%u epoch=%u valid=%llx writes=0 voltage-raw=%u current-raw=%d temperature-raw=%d\n",
        poll.cycles,poll.epoch,poll.committed.valid,
        poll.committed.value[EldenChargeVoltage].raw,
        (int)poll.committed.value[EldenChargeCurrent].signed_value,
        (int)poll.committed.value[EldenChargeTemperature].signed_value);
    report("\nELDEN_PMIC usb1=%u usb2=%u suspend=%u charging-enable=%u pps=%u; fg1-v=%u fg2-v=%u fg1-i=%d fg2-i=%d\n",
        poll.committed.value[EldenChargeUsb1Online].raw,poll.committed.value[EldenChargeUsb2Online].raw,
        poll.committed.value[EldenChargeInputSuspended].raw,poll.committed.value[EldenChargeEnabled].raw,
        poll.committed.value[EldenChargePpsStatus].raw,poll.committed.value[EldenChargeFg1Voltage].raw,
        poll.committed.value[EldenChargeFg2Voltage].raw,
        (int)poll.committed.value[EldenChargeFg1Current].signed_value,
        (int)poll.committed.value[EldenChargeFg2Current].signed_value);
}
static void worker(void *,wait_result_t) {
    for(unsigned i=0;i<12000 && (!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted);++i)IOSleep(10);
    if(!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted) {
        report("\nELDEN_PMIC refused baseline gate\n");thread_terminate(current_thread());return;
    }
    static const ShengAdspClient client={state,receive};
    for(unsigned i=0;i<480 && !channel;++i) {
        channel=sheng_adsp_channel("PMIC_RTR_ADSP_APPS",1024,4,0,&client,nullptr);
        if(!channel)IOSleep(250);
    }
    if(!channel) {report("\nELDEN_PMIC refused no ADSP channel\n");thread_terminate(current_thread());return;}
    elden_pmic_stats[0]=1;
    EldenChargePoll poll;elden_charge_poll_init(&poll,true);
    unsigned seen=0;uint64_t deadline=0,nextRefresh=0;
    for(;;) {
        IOLockLock(lock);
        const unsigned epoch=__atomic_load_n(&linkEpoch,__ATOMIC_ACQUIRE);
        const bool nowUp=__atomic_load_n(&linkUp,__ATOMIC_ACQUIRE)!=0;
        if(epoch!=seen) {
            /* Drop queued bytes from the prior transport epoch before a new query. */
            head=used=0;
            elden_charge_poll_link(&poll,false);
            elden_charge_poll_link(&poll,nowUp);
            __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
            __atomic_store_n(&overflow,0u,__ATOMIC_RELEASE);
            seen=epoch;deadline=nextRefresh=0;elden_pmic_stats[1]=epoch;
        }
        IOLockUnlock(lock);
        /* Process queued completions before declaring a timeout. */
        for(;;) {
            Incoming message;bool have=false;
            IOLockLock(lock);
            if(used) {message=inbox[head];head=(head+1)%8;--used;have=true;}
            IOLockUnlock(lock);if(!have)break;
            if(message.epoch!=seen)continue;
            int result=elden_charge_poll_receive(&poll,message.bytes,message.size);
            if(!poll.pending)deadline=0;
            if(result==EldenChargePollCycle) {
                publishRaw(poll);clock_interval_to_deadline(3000,kMillisecondScale,&nextRefresh);
            }
        }
        if(__atomic_exchange_n(&overflow,0u,__ATOMIC_ACQ_REL) && poll.pending) elden_charge_poll_timeout(&poll);
        const uint64_t now=mach_absolute_time();
        if(poll.pending && deadline && now>=deadline) elden_charge_poll_timeout(&poll);
        if(poll.quarantined) {
            __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
            if(elden_pmic_stats[0]!=3) {
                elden_pmic_stats[0]=3;elden_pmic_stats[8]=(unsigned)poll.failed_field;
                report("\nELDEN_PMIC stopped field=%d timeout=%u errors=%u; fresh GLINK epoch required\n",
                    poll.failed_field,poll.timeouts,poll.errors);
            }
        } else {
            if(poll.up && !poll.active && (!nextRefresh || now>=nextRefresh))elden_charge_poll_refresh(&poll);
            uint8_t request[24];int bytes=elden_charge_poll_next(&poll,request);
            if(bytes==24) {
                if(sheng_adsp_send_epoch(channel,seen,request,sizeof(request))) elden_charge_poll_unsent(&poll);
                else {clock_interval_to_deadline(1000,kMillisecondScale,&deadline);elden_pmic_stats[0]=2;}
            }
        }
        assert_wait_timeout((event_t)&linkEpoch,THREAD_UNINT,poll.pending?5:50,kMillisecondScale);
        thread_block(THREAD_CONTINUE_NULL);
    }
}
}
extern "C" void elden_pmic_client_start(void) {
    unsigned enable=0,adsp=0;
    PE_parse_boot_argn("elden-pmic",&enable,sizeof(enable));PE_parse_boot_argn("elden-adsp",&adsp,sizeof(adsp));
    if(enable!=1 || adsp!=2 || __atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
    lock=IOLockAlloc();thread_t thread=nullptr;
    if(!lock || kernel_thread_start(worker,nullptr,&thread)!=KERN_SUCCESS) {
        report("\nELDEN_PMIC refused worker allocation\n");return;
    }
    thread_deallocate(thread);
}
