/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Elden board client of the canonical sheng ADSP transport. Explicit opt-in
 * telemetry, stock notification/PPM initialization, no charger policy SET.
 * Only a qualified board publisher consumes these raw coherent snapshots.
 */
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include "ShengAdsp.h"
#include "ChargePoll.h"
#include "ChargeNotify.h"
#include "UsbIcl.h"
#include "ChargeVotes.h"
#include "PdContract.h"
#include "PdBootstrap.h"
#include "PowerPathRead.h"
#include "PowerPathSchedule.h"
#include "DiagnosticGate.h"
#include "DcNoop.h"
#include "UlogRead.h"
#include "UlogFilter.h"
#include "ChargePolicy.h"
#include "UcsiQuery.h"
#ifndef ELDEN_BATTERY_RUNTIME
#define ELDEN_BATTERY_RUNTIME 0
#endif
#if ELDEN_BATTERY_RUNTIME
#include "BatteryRuntime.h"
#endif
#include <stdarg.h>
extern "C" {
int elden_recovery_armed(void);
extern volatile int native_owner_data_status, elden_init_exec_accepted;
void elden_usb_log_putc(char);
uint32_t elden_usb_power_budget_ua(void);
void elden_cpu_perf_survey_start(void);
void elden_adc_runtime_start(void);
void elden_thermal_runtime_start(void);
unsigned elden_recovery_deadline_remaining(void) __attribute__((weak_import));
volatile uint32_t elden_pmic_stats[12];
volatile uint32_t elden_pmic_raw[EldenChargeFieldCount];
volatile uint64_t elden_pmic_valid;
volatile uint32_t elden_pmic_sequence;
volatile uint32_t elden_ucsi_stats[10], elden_ucsi_ports[2][6];
}
namespace {
struct Incoming {uint32_t epoch, size;uint8_t bytes[SHENG_ADSP_MAX_MESSAGE];};
static Incoming inbox[8];
static Incoming logsInbox[8];
/* Separate bounded storage; neither the PMIC queue nor a kernel stack grows.
 * The callback only copies. Validation/output runs on the PMIC worker. */
struct UlogIncoming {uint32_t epoch,size;uint8_t bytes[ELDEN_ULOG_REPLY];};
static UlogIncoming ulogInbox;
static EldenUlogRead ulog;
static unsigned ulogInboxUsed,ulogInboxError;
static volatile unsigned ulogReadsEnabled;
static EldenLogFilter logFilter;
static volatile unsigned logFilterEnabled,logFilterStop,logFilterTerminal,logFilterStopOk;
static unsigned head, used, attempted;
static unsigned logsHead,logsUsed;
static unsigned usbIclSends;
static EldenChargeVotes chargeVotes;
static EldenChargePolicy chargePolicy;
static volatile unsigned policyAvailable,policyRequested,policyRequestSequence;
static volatile unsigned bootstrapEnabled,bootstrapStop,bootstrapStopped;
static volatile unsigned registerReadsEnabled;
static EldenDcNoop dcNoop;
static volatile unsigned dcNoopEnabled,dcNoopStop,dcNoopTerminal,dcNoopStopOk;
static unsigned policyApplyCounter;
static volatile uint64_t policyPublicState;
static IOLock *lock;
static volatile unsigned linkUp, linkEpoch, overflow;
static volatile unsigned logsUp,logsEpoch,logsOverflow;
static ShengAdspChannel *channel;
static ShengAdspChannel *logsChannel;
static uint32_t messageOwner(const uint8_t *data) {
    return data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
}
static void report(const char *format,...) {
    char text[320];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    for(const char *p=text;*p;++p)elden_usb_log_putc(*p);
}
static uint64_t milliseconds(uint64_t absolute){uint64_t ns=0;absolutetime_to_nanoseconds(absolute,&ns);return ns/1000000;}

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
static void logsState(void *,int value) {
    IOLockLock(lock);
    if(__atomic_exchange_n(&logsUp,value!=0,__ATOMIC_ACQ_REL)!=(unsigned)(value!=0))
        __atomic_add_fetch(&logsEpoch,1,__ATOMIC_RELEASE);
    IOLockUnlock(lock);thread_wakeup((event_t)&linkEpoch);
}
static void logsReceive(void *,const uint8_t *data,uint32_t size) {
    /* Only requested bounded read protocols enter this separate inbox. */
    if(size<4)return;
    unsigned owner=messageOwner(data);
    if(owner==ELDEN_ULOG_OWNER&&__atomic_load_n(&ulogReadsEnabled,__ATOMIC_ACQUIRE)){
        IOLockLock(lock);
        bool filterReply=__atomic_load_n(&logFilterEnabled,__ATOMIC_ACQUIRE)&&size==16&&messageOwner(data+8)==0x19;
        if(size!=ELDEN_ULOG_REPLY&&!filterReply)ulogInboxError=EldenUlogLength;
        else if(ulogInboxUsed)ulogInboxError=EldenUlogOverflow;
        else {
            ulogInbox.epoch=__atomic_load_n(&logsEpoch,__ATOMIC_ACQUIRE);
            ulogInbox.size=size;
            memcpy(ulogInbox.bytes,data,size);ulogInboxUsed=1;
        }
        IOLockUnlock(lock);thread_wakeup((event_t)&linkEpoch);return;
    }
    if(owner!=ELDEN_VOTE_OWNER&&!(owner==ELDEN_POWER_REG_OWNER&&
       (__atomic_load_n(&registerReadsEnabled,__ATOMIC_ACQUIRE)||__atomic_load_n(&dcNoopEnabled,__ATOMIC_ACQUIRE))))return;
    IOLockLock(lock);
    if(size>SHENG_ADSP_MAX_MESSAGE||logsUsed==8)__atomic_store_n(&logsOverflow,1,__ATOMIC_RELEASE);
    else {
        Incoming &m=logsInbox[(logsHead+logsUsed)%8];m.epoch=__atomic_load_n(&logsEpoch,__ATOMIC_ACQUIRE);
        m.size=size;memcpy(m.bytes,data,size);++logsUsed;
    }
    IOLockUnlock(lock);thread_wakeup((event_t)&linkEpoch);
}
static void publishVotes(void) {
    for(unsigned i=0;i<chargeVotes.tables;++i){
        const EldenVote &v=chargeVotes.votes[i];
        report("\nELDEN_VOTE cycle=%u name=%s winner=%s value=%u mask=%x override=%u; GET-only raw units\n",
            chargeVotes.cycle,chargeVotes.names[i],v.mask?chargeVotes.clients[v.winner]:"none",
            v.effective,v.mask,v.override_id);
        for(unsigned j=0;j<chargeVotes.voters;++j)if(v.mask&(UINT32_C(1)<<j))
            report("\nELDEN_VOTE_INPUT name=%s client=%s value=%u\n",chargeVotes.names[i],chargeVotes.clients[j],v.values[j]);
    }
}
static bool onlySdpInputVote(void){
    if(!chargeVotes.ready||chargeVotes.failed)return false;
    int table=-1,client=-1;
    for(unsigned i=0;i<chargeVotes.tables;i++)if(!strcmp(chargeVotes.names[i],"iusb0_tot")){if(table>=0)return false;table=(int)i;}
    for(unsigned i=0;i<chargeVotes.voters;i++)if(!strcmp(chargeVotes.clients[i],"i_sdp")){if(client>=0)return false;client=(int)i;}
    if(table<0||client<0||client>=32)return false;
    const EldenVote &v=chargeVotes.votes[table];
    return v.override_id==UINT32_MAX&&v.mask==(UINT32_C(1)<<client)&&v.winner==(unsigned)client&&v.effective==500&&v.values[client]==500;
}
static void publishRaw(const EldenChargePoll &poll,uint64_t ageMs) {
    __atomic_add_fetch(&elden_pmic_sequence,1u,__ATOMIC_ACQ_REL);
    __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
    for(unsigned i=0;i<EldenChargeFieldCount;++i)elden_pmic_raw[i]=poll.committed.value[i].raw;
    __atomic_store_n(&elden_pmic_valid,poll.committed.valid,__ATOMIC_RELEASE);
    __atomic_add_fetch(&elden_pmic_sequence,1u,__ATOMIC_RELEASE);
    elden_pmic_stats[2]=poll.cycles;elden_pmic_stats[3]=poll.responses;
    elden_pmic_stats[4]=poll.errors;elden_pmic_stats[5]=poll.timeouts;
    elden_pmic_stats[6]=poll.notifications;elden_pmic_stats[7]=poll.ignored;
    report("\nELDEN_PMIC cycle=%u epoch=%u valid=%llx usb-icl-sets=%u voltage-raw=%u current-raw=%d temperature-raw=%d\n",
        poll.cycles,poll.epoch,poll.committed.valid,usbIclSends,
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
    report("\nELDEN_POWER_INPUTS USB-v=%u i=%d limit=%u temp=%d real-type=%u WLS-online=%u v=%u i=%d limit=%u temp=%d Lenovo55=%u; raw-units, mapping-pending\n",
        poll.committed.value[EldenChargeUsbVoltage].raw,(int)poll.committed.value[EldenChargeUsbCurrent].signed_value,
        poll.committed.value[EldenChargeUsbLimit].raw,(int)poll.committed.value[EldenChargeUsbTemperature].signed_value,
        poll.committed.value[EldenChargeUsbRealType].raw,poll.committed.value[EldenChargeWlsOnline].raw,
        poll.committed.value[EldenChargeWlsVoltage].raw,(int)poll.committed.value[EldenChargeWlsCurrent].signed_value,
        poll.committed.value[EldenChargeWlsLimit].raw,(int)poll.committed.value[EldenChargeWlsTemperature].signed_value,
        poll.committed.value[EldenChargeSc7603Lowpower].raw);
    /* Complete scalar transcript for an OEM comparison, in bounded USB lines.
     * Keep raw two's-complement bits and per-field validity; no unit guesses. */
    for(unsigned first=0;first<EldenChargeFieldCount;first+=9) {
        char line[256];int n=snprintf(line,sizeof(line),
            "\nELDEN_PMIC_RAW cycle=%u epoch=%u age-ms=%llu first=%u valid=%llx values=",
            poll.committed.cycle,poll.committed.epoch,ageMs,first,poll.committed.valid);
        for(unsigned i=first;i<EldenChargeFieldCount && i<first+9 && n>0 && n<(int)sizeof(line);++i)
            n+=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%08x",i==first?"":",",poll.committed.value[i].raw);
        if(n>0 && n<(int)sizeof(line)-1){line[n++]='\n';line[n]=0;report("%s",line);}
    }
#if ELDEN_BATTERY_RUNTIME
    elden_battery_runtime_update(&poll.committed,ageMs);
#endif
}
static void publishUcsi(const EldenUcsiQuery &q) {
    elden_ucsi_stats[0]=2;elden_ucsi_stats[1]=q.working.version;elden_ucsi_stats[2]=q.buffer_bytes;
    elden_ucsi_stats[3]=q.cycles;elden_ucsi_stats[4]=q.cci;elden_ucsi_stats[5]=q.errors;
    for(unsigned port=0;port<2;++port) {
        const EldenUcsiPort &p=q.committed.port[port];
        elden_ucsi_ports[port][0]=p.flags;elden_ucsi_ports[port][1]=p.rdo;
        elden_ucsi_ports[port][2]=p.capability;elden_ucsi_ports[port][3]=p.status_bytes;
        elden_ucsi_ports[port][4]=p.sink_path_known;elden_ucsi_ports[port][5]=p.sink_path_enabled;
        report("\nELDEN_UCSI port=%u flags=%x RDO=%x cap=%x status-bytes=%u connected=%u USB=%u power-mode=%u sink-known=%u sink-enabled=%u\n",
            port+1,p.flags,p.rdo,p.capability,p.status_bytes,(unsigned)p.connected,(unsigned)p.usb,p.power_mode,
            (unsigned)p.sink_path_known,(unsigned)p.sink_path_enabled);
        if(q.query_pdos){
            report("\nELDEN_PD port=%u pdos=%u complete=%u consistent=%u error=%u rdo=%08x\n",
                port+1,p.pdo_count,p.pdos_complete,p.pdos_consistent,p.pdos_error,p.rdo);
            for(unsigned i=0;i<p.pdo_count;i++)report("\nELDEN_PD_SOURCE port=%u position=%u raw=%08x\n",port+1,i+1,p.source_pdos[i]);
            EldenPdContract contract={};int rc=p.pdos_consistent?elden_pd_contract(p.source_pdos,p.pdo_count,p.rdo,&contract):-11;
            report("\nELDEN_PD_CONTRACT port=%u result=%d position=%u type=%u range-mv=%u/%u exact-v=%u requested-mv=%u requested-ma=%u requested-mw=%u; contract-only, not metering\n",
                port+1,rc,contract.position,contract.type,contract.min_mv,contract.max_mv,contract.exact_voltage,
                contract.requested_mv,contract.requested_ma,contract.requested_mw);
        }
    }
    report("\nELDEN_UCSI cycle=%u version=%x buffer=%u connectors=%u ppm-init=%u; GET/ACK cycle, no role/sink change\n",
        q.cycles,q.working.version,q.buffer_bytes,q.committed.connectors,(unsigned)q.initialize_ppm);
    if(q.connector_events)report("\nELDEN_UCSI_EVENTS ready=%u mask=%x connector-acks=%u pending=%u notifications=%u\n",
        (unsigned)q.events_ready,q.notification_control>>16,q.connector_acks,(unsigned)q.active_connector,q.notifications);
}
static void worker(void *,wait_result_t) {
    for(unsigned i=0;i<12000 && (!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted);++i)IOSleep(10);
    if(!elden_recovery_armed() || native_owner_data_status!=0 || !elden_init_exec_accepted) {
        report("\nELDEN_PMIC refused baseline gate\n");thread_terminate(current_thread());return;
    }
#if ELDEN_BATTERY_RUNTIME
    elden_battery_runtime_configure();
#endif
    static const ShengAdspClient client={state,receive};
    for(unsigned i=0;i<480 && !channel;++i) {
        channel=sheng_adsp_channel("PMIC_RTR_ADSP_APPS",1024,4,0,&client,nullptr);
        if(!channel)IOSleep(250);
    }
    if(!channel) {report("\nELDEN_PMIC refused no ADSP channel\n");thread_terminate(current_thread());return;}
    elden_pmic_stats[0]=1;
    elden_cpu_perf_survey_start();
    elden_adc_runtime_start();
    elden_thermal_runtime_start();
    EldenChargePoll poll;elden_charge_poll_init(&poll,true);
    unsigned notifyEnable=0;PE_parse_boot_argn("elden-pmic-notify",&notifyEnable,sizeof(notifyEnable));
    EldenChargeNotify notify;elden_charge_notify_init(&notify);
    uint64_t notifyDeadline=0,notifySettle=0;bool notifyReported=false;
    unsigned iclEnable=0;PE_parse_boot_argn("elden-usb-icl",&iclEnable,sizeof(iclEnable));
    EldenUsbIcl icl;elden_usb_icl_init(&icl);uint64_t iclDeadline=0;bool iclReported=false;
    elden_usb_icl_sdp_only(&icl,iclEnable==2);if(iclEnable==2)iclEnable=1;
    unsigned iclDeferredCycle=0;
    unsigned votesEnable=0;PE_parse_boot_argn("elden-power-votes",&votesEnable,sizeof(votesEnable));
    unsigned regsEnable=0;PE_parse_boot_argn("elden-regs",&regsEnable,sizeof(regsEnable));
    regsEnable=((regsEnable==1||regsEnable==2)&&votesEnable==1)?regsEnable:0u;
    __atomic_store_n(&registerReadsEnabled,regsEnable,__ATOMIC_RELEASE);
    EldenPowerPathRead registers;elden_power_reg_init(&registers);
    if(regsEnable==2)elden_power_reg_extend(&registers);
    EldenPowerPathSchedule regsSchedule={};
    unsigned dcEnable=0;PE_parse_boot_argn("elden-dc-noop",&dcEnable,sizeof(dcEnable));
    dcEnable=(dcEnable==1&&votesEnable==1&&!regsEnable)?1u:0u;
    elden_dc_noop_init(&dcNoop);EldenPowerPathSchedule dcSchedule={};
    uint64_t dcDeadline=0,votesCaptured=0;bool dcReported=false;
    unsigned ulogEnable=0;PE_parse_boot_argn("elden-ulog",&ulogEnable,sizeof(ulogEnable));
    ulogEnable=((ulogEnable>=1&&ulogEnable<=3)&&votesEnable==1&&regsEnable==0)?ulogEnable:0u;
    if(ulogEnable)dcEnable=0; /* diagnostic protocols cannot share a write owner */
    elden_ulog_init(&ulog);
    if(ulogEnable==2)elden_ulog_enable_paging(&ulog);
    if(ulogEnable==3)elden_ulog_enable_policy_trace(&ulog);
    elden_log_filter_init(&logFilter);uint64_t filterDeadline=0,policyReadAt=0;unsigned filterSeen=0;
    __atomic_store_n(&logFilterEnabled,ulogEnable==3,__ATOMIC_RELEASE);
    __atomic_store_n(&ulogReadsEnabled,ulogEnable,__ATOMIC_RELEASE);
    EldenPowerPathSchedule ulogSchedule={};
    uint64_t ulogDeadline=0;unsigned ulogOffset=0;bool ulogReported=false;
    uint64_t regsDeadline=0,regsRefresh=0;bool regsReported=false;
    elden_charge_votes_init(&chargeVotes);uint64_t votesDeadline=0,votesRefresh=0,votesReadyAt=0;bool votesReported=false;
    unsigned logsSeen=0;
    unsigned policyEnable=0;PE_parse_boot_argn("elden-charge-control",&policyEnable,sizeof(policyEnable));
    elden_charge_policy_init(&chargePolicy);uint64_t policyDeadline=0,policyReadyAt=0;
    unsigned policySeenRequest=0,policySeenApplied=0;bool policyReported=false;
    if(votesEnable==1){
        static const ShengAdspClient logsClient={logsState,logsReceive};
        logsChannel=sheng_adsp_channel("PMIC_LOGS_ADSP_APPS",ulogEnable?ELDEN_ULOG_REPLY:2048,5,0,&logsClient,nullptr);
        report("\nELDEN_VOTE channel=PMIC_LOGS_ADSP_APPS registered=%u; separate epoch/inbox\n",logsChannel?1u:0u);
        if(ulogEnable)report("\nELDEN_ULOG configured reply=%u slots=1 cap=%u; INIT_GET/CURRENT_GET only, no SET\n",ELDEN_ULOG_REPLY,ulogEnable==2?ELDEN_ULOG_MAX_PAGES:2u);
    }
    unsigned ucsiEnable=0;PE_parse_boot_argn("elden-ucsi-query",&ucsiEnable,sizeof(ucsiEnable));
    EldenUcsiQuery ucsi;elden_ucsi_init(&ucsi);
    unsigned ucsiInit=0;PE_parse_boot_argn("elden-ucsi-init",&ucsiInit,sizeof(ucsiInit));
    elden_ucsi_initialize_ppm(&ucsi,ucsiInit==1||ucsiInit==2);
    elden_ucsi_enable_events(&ucsi,ucsiInit==2);
    unsigned pdos=0;PE_parse_boot_argn("elden-ucsi-pdos",&pdos,sizeof(pdos));
    elden_ucsi_enable_pdos(&ucsi,pdos==1);
    unsigned bootMode=0,thermalMode=0;PE_parse_boot_argn("elden-pd-bootstrap",&bootMode,sizeof(bootMode));
    PE_parse_boot_argn("elden-thermal",&thermalMode,sizeof(thermalMode));
    if((bootMode!=1&&bootMode!=2)||thermalMode!=3||iclEnable!=1||notifyEnable!=1||ucsiEnable!=1||pdos!=1)bootMode=0;
    dcEnable=dcEnable&&bootMode==2&&thermalMode==3&&ucsiInit==2;
    __atomic_store_n(&dcNoopEnabled,dcEnable,__ATOMIC_RELEASE);
    EldenPdBootstrap bootstrap;elden_pd_boot_init(&bootstrap,bootMode,milliseconds(mach_absolute_time()));
    unsigned bootstrapSeen=EldenPdBootOff,bootstrapCycle=0;
    __atomic_store_n(&bootstrapEnabled,bootMode,__ATOMIC_RELEASE);
    unsigned seen=0;uint64_t deadline=0,nextRefresh=0,ucsiDeadline=0,ucsiRefresh=0,ucsiTick=0;
    uint64_t cycleStart=0,committedExpiry=0,ucsiCommittedExpiry=0;bool staleReported=false;
    for(;;) {
        IOLockLock(lock);
        const unsigned epoch=__atomic_load_n(&linkEpoch,__ATOMIC_ACQUIRE);
        const bool nowUp=__atomic_load_n(&linkUp,__ATOMIC_ACQUIRE)!=0;
        if(epoch!=seen) {
            /* Drop queued bytes from the prior transport epoch before a new query. */
            head=used=0;
            elden_charge_poll_link(&poll,false);
            elden_charge_poll_link(&poll,nowUp);
            if(notifyEnable==1){elden_charge_notify_link(&notify,false);elden_charge_notify_link(&notify,nowUp);}
            notifyDeadline=notifySettle=0;notifyReported=false;
            if(iclEnable==1){elden_usb_icl_link(&icl,false);elden_usb_icl_link(&icl,nowUp);}
            iclDeadline=0;iclReported=false;
            if(policyEnable==1){elden_charge_policy_link(&chargePolicy,false);elden_charge_policy_link(&chargePolicy,nowUp);}
            policyDeadline=policyReadyAt=0;policyReported=false;__atomic_store_n(&policyAvailable,0,__ATOMIC_RELEASE);
            if(nowUp)clock_interval_to_deadline(15000,kMillisecondScale,&policyReadyAt);
            if(ucsiEnable==1){elden_ucsi_link(&ucsi,false);elden_ucsi_link(&ucsi,nowUp);}
            if(ucsiEnable==1){elden_ucsi_stats[0]=nowUp?1u:0u;elden_ucsi_stats[1]=elden_ucsi_stats[2]=0;}
            __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
            __atomic_store_n(&overflow,0u,__ATOMIC_RELEASE);
            seen=epoch;deadline=nextRefresh=ucsiDeadline=ucsiRefresh=ucsiTick=0;elden_pmic_stats[1]=epoch;
            cycleStart=committedExpiry=ucsiCommittedExpiry=0;staleReported=false;
#if ELDEN_BATTERY_RUNTIME
            elden_battery_runtime_unavailable(poll.epoch,EldenBatteryStale);
#endif
        }
        const unsigned logEpoch=__atomic_load_n(&logsEpoch,__ATOMIC_ACQUIRE);
        const bool logUp=__atomic_load_n(&logsUp,__ATOMIC_ACQUIRE)!=0;
        if(votesEnable==1&&logEpoch!=logsSeen){
            logsHead=logsUsed=0;__atomic_store_n(&logsOverflow,0,__ATOMIC_RELEASE);
            elden_charge_votes_link(&chargeVotes,false);elden_charge_votes_link(&chargeVotes,logUp);
            votesDeadline=votesRefresh=votesReadyAt=0;votesReported=false;logsSeen=logEpoch;
            if(logUp)clock_interval_to_deadline(15000,kMillisecondScale,&votesReadyAt);
            if(regsEnable){elden_power_reg_link(&registers,logUp,logEpoch);regsDeadline=regsRefresh=0;regsReported=false;regsSchedule.ready_at_ms=0;}
            if(ulogEnable){
                elden_ulog_link(&ulog,logUp,logEpoch);ulogInboxUsed=ulogInboxError=0;
                ulogDeadline=0;ulogOffset=0;ulogReported=false;ulogSchedule.ready_at_ms=0;
                if(ulogEnable==3){elden_log_filter_link(&logFilter,logUp,logEpoch);filterDeadline=0;policyReadAt=0;}
            }
            if(dcEnable){elden_dc_noop_link(&dcNoop,logUp,logEpoch);dcDeadline=0;votesCaptured=0;dcSchedule.ready_at_ms=0;}
        }
        IOLockUnlock(lock);
        /* Process queued completions before declaring a timeout. */
        for(;;) {
            Incoming message;bool have=false;
            IOLockLock(lock);
            if(used) {message=inbox[head];head=(head+1)%8;--used;have=true;}
            IOLockUnlock(lock);if(!have)break;
            if(message.epoch!=seen)continue;
            if(ucsiEnable==1 && message.size>=4 && messageOwner(message.bytes)==ELDEN_UCSI_OWNER) {
                int result=elden_ucsi_receive(&ucsi,message.bytes,message.size);
                if(!ucsi.pending)ucsiDeadline=0;
                if(result==EldenUcsiCycle){publishUcsi(ucsi);clock_interval_to_deadline(3000,kMillisecondScale,&ucsiRefresh);clock_interval_to_deadline(10000,kMillisecondScale,&ucsiCommittedExpiry);}
                continue;
            }
            if(notifyEnable==1 && message.size>=12 && messageOwner(message.bytes)==ELDEN_CHARGE_OWNER &&
               messageOwner(message.bytes+8)==4) {
                int result=elden_charge_notify_receive(&notify,message.bytes,message.size);
                if(result) {
                    notifyDeadline=0;
                    if(result==1)clock_interval_to_deadline(200,kMillisecondScale,&notifySettle);
                    report("\nELDEN_PMIC_NOTIFY ack=%d epoch=%u bytes=%u type=%u word=%x; stock zero-payload registration\n",
                        result,notify.epoch,notify.reply_bytes,notify.reply_type,notify.reply_word);
                }
                continue;
            }
            if(iclEnable==1 && message.size>=12 && messageOwner(message.bytes)==ELDEN_CHARGE_OWNER &&
               messageOwner(message.bytes+8)==0x33) {
                int result=elden_usb_icl_receive(&icl,message.bytes,message.size);
                if(result) {
                    iclDeadline=0;
                    report("\nELDEN_USB_ICL ack=%d requested=%u applied=%u ret=%x bytes=%u; verify next GET\n",
                        result,icl.requested,icl.applied,icl.ret_code,message.size);
                }
                continue;
            }
            if(policyEnable==1&&message.size>=16&&messageOwner(message.bytes)==ELDEN_CHARGE_OWNER){
                uint32_t op=messageOwner(message.bytes+8),prop=messageOwner(message.bytes+12);
                if((op==0x60&&(prop==29||prop==30))||op==0x61){
                    int result=elden_charge_policy_receive(&chargePolicy,message.bytes,message.size);
                    if(!chargePolicy.pending)policyDeadline=0;
                    report("\nELDEN_CHARGE reply op=%x property=%u result=%d phase=%u\n",op,prop,result,(unsigned)chargePolicy.phase);
                    continue;
                }
            }
            int result=elden_charge_poll_receive(&poll,message.bytes,message.size);
            if(!poll.pending)deadline=0;
            if(result==EldenChargePollCycle) {
                uint64_t nanos=0;const uint64_t completed=mach_absolute_time();
                if(cycleStart && completed>=cycleStart)absolutetime_to_nanoseconds(completed-cycleStart,&nanos);
                const uint64_t ageMs=cycleStart?nanos/1000000:UINT64_MAX;
                publishRaw(poll,ageMs);
                if(policyEnable==1&&poll.committed.valid==((UINT64_C(1)<<EldenChargeFieldCount)-1))
                    elden_charge_policy_observe(&chargePolicy,poll.committed.cycle,
                        poll.committed.value[EldenChargeCapacity].raw,poll.committed.value[EldenChargeEnabled].raw);
                clock_interval_to_deadline(3000,kMillisecondScale,&nextRefresh);
                const uint32_t remaining=ageMs>=10000?0u:10000u-(uint32_t)ageMs;
                clock_interval_to_deadline(remaining,kMillisecondScale,&committedExpiry);
                cycleStart=poll.active?completed:0;staleReported=false;
            }
        }
        for(;;){
            Incoming message;bool have=false;
            IOLockLock(lock);
            if(logsUsed){message=logsInbox[logsHead];logsHead=(logsHead+1)%8;--logsUsed;have=true;}
            IOLockUnlock(lock);if(!have)break;
            if(message.epoch!=logsSeen||message.size<12)continue;
            if(messageOwner(message.bytes)==ELDEN_POWER_REG_OWNER){
                if(dcEnable){
                    int result=elden_dc_noop_receive(&dcNoop,message.bytes,message.size);
                    if(!dcNoop.pending)dcDeadline=0;
                    if(result==2)report("\nELDEN_DC_NOOP pre-read accepted epoch=%u value=%u; write still needs fresh guards\n",dcNoop.epoch,dcNoop.snapshot.values[3]);
                    if(result==3)report("\nELDEN_DC_NOOP VERIFIED epoch=%u value=10 ma=500 writes=1; same-value test only\n",dcNoop.epoch);
                    continue;
                }
                if(regsEnable){
                    int result=elden_power_reg_receive(&registers,message.bytes,message.size);
                    if(!registers.pending)regsDeadline=0;
                    if(result<0)report("\nELDEN_POWER_REG rejected bytes=%u type=%u op=%x bus=%u sid=%u addr=%x count=%x\n",
                        message.size,messageOwner(message.bytes+4),messageOwner(message.bytes+8),
                        message.size>=28?messageOwner(message.bytes+12):0,message.size>=28?messageOwner(message.bytes+16):0,
                        message.size>=28?messageOwner(message.bytes+20):0,message.size>=28?messageOwner(message.bytes+24):0);
                    if(result==2){
                        const uint8_t *v=registers.values;
                        unsigned last=registers.extended?18:3,usb=registers.extended?2:1,dc=registers.extended?3:2;
                        report("\nELDEN_POWER_PATH cycle=%u epoch=%u path=%02x/%02x usb50=%u wls50=%u stable=%u selection=%u; GET-only bus0 SID7\n",
                            registers.cycles,registers.epoch,v[0],v[last],v[usb],v[dc],((v[0]^v[last])&0x30)==0,(v[last]>>4)&3);
                        if(registers.extended){
                            report("\nELDEN_GPIO_PATH cycle=%u suspend=%02x; GPIO indices are zero-based; register readings do not prove reverse blocking\n",registers.cycles,v[1]);
                            for(unsigned g=0;g<2;g++){
                                unsigned p=4+7*g;
                                report("\nELDEN_GPIO_REG index=%u base=%04x type=%02x subtype=%02x mode=%02x vin=%02x source=%02x enable=%02x status=%02x\n",
                                    9+g,0x9100+g*0x100,v[p],v[p+1],v[p+2],v[p+3],v[p+4],v[p+5],v[p+6]);
                            }
                        }
                        clock_interval_to_deadline(5000,kMillisecondScale,&regsRefresh);
                    }
                }
                continue;
            }
            int result=elden_charge_votes_receive(&chargeVotes,message.bytes,message.size);
            if(chargeVotes.cycle==0||result<0)
                report("\nELDEN_VOTE reply bytes=%u type=%u opcode=%x result=%d\n",
                    message.size,messageOwner(message.bytes+4),messageOwner(message.bytes+8),result);
            if(!chargeVotes.pending)votesDeadline=0;
            if(result==2){votesCaptured=mach_absolute_time();publishVotes();clock_interval_to_deadline(10000,kMillisecondScale,&votesRefresh);}
        }
        if(__atomic_exchange_n(&logsOverflow,0u,__ATOMIC_ACQ_REL)){
            if(chargeVotes.pending)elden_charge_votes_timeout(&chargeVotes);
            if(regsEnable)elden_power_reg_fail(&registers,EldenPowerRegOverflow);
            if(dcEnable&&dcNoop.pending)elden_dc_noop_fail(&dcNoop,EldenDcNoopProtocol);
        }
        if(ulogEnable){
            IOLockLock(lock);
            if(ulogInboxError){elden_ulog_fail(&ulog,ulogInboxError);if(ulogEnable==3)elden_log_filter_fail(&logFilter,3);ulogInboxError=0;}
            if(ulogInboxUsed){
                if(ulogInbox.size==16&&ulogEnable==3){elden_log_filter_receive(&logFilter,ulogInbox.epoch,ulogInbox.bytes,ulogInbox.size);if(!logFilter.pending)filterDeadline=0;}
                else{int result=elden_ulog_receive(&ulog,ulogInbox.epoch,ulogInbox.bytes,ulogInbox.size);if(result==1)ulogOffset=0;}
                ulogInboxUsed=0;
            }
            IOLockUnlock(lock);
            if(!ulog.pending)ulogDeadline=0;
        }
        if(__atomic_exchange_n(&overflow,0u,__ATOMIC_ACQ_REL)) {
            if(poll.pending)elden_charge_poll_timeout(&poll);
            if(ucsi.pending)elden_ucsi_timeout(&ucsi);
            if(notify.pending)elden_charge_notify_timeout(&notify);
            if(icl.pending)elden_usb_icl_timeout(&icl);
            if(chargePolicy.pending)elden_charge_policy_timeout(&chargePolicy);
        }
        const uint64_t now=mach_absolute_time();
        if(notifyEnable==1) {
            if(notify.pending && notifyDeadline && now>=notifyDeadline)elden_charge_notify_timeout(&notify);
            if(notify.quarantined && !notifyReported) {
                report("\nELDEN_PMIC_NOTIFY stopped epoch=%u timeouts=%u bytes=%u type=%u word=%x; GET telemetry continues\n",
                    notify.epoch,notify.timeouts,notify.reply_bytes,notify.reply_type,notify.reply_word);
                notifyReported=true;
            }
            uint8_t request[28];int bytes=elden_charge_notify_next(&notify,request);
            if(bytes==28) {
                if(sheng_adsp_send_epoch(channel,seen,request,sizeof(request)))elden_charge_notify_unsent(&notify);
                else clock_interval_to_deadline(1000,kMillisecondScale,&notifyDeadline);
            }
        }
#if ELDEN_BATTERY_RUNTIME
        if(committedExpiry && now>=committedExpiry && !staleReported) {
            elden_battery_runtime_unavailable(poll.epoch,EldenBatteryStale);staleReported=true;
        }
#else
        (void)committedExpiry;(void)staleReported;
#endif
        if(poll.pending && deadline && now>=deadline) elden_charge_poll_timeout(&poll);
        if(ucsiEnable==1) {
            if(ucsi.pending && ucsiDeadline && now>=ucsiDeadline)elden_ucsi_timeout(&ucsi);
            if(ucsi.phase==EldenUcsiFailed) {
                if(elden_ucsi_stats[0]!=3) {
                    elden_ucsi_stats[0]=3;elden_ucsi_stats[5]=ucsi.errors;elden_ucsi_stats[6]=ucsi.firmware_error;
                    report("\nELDEN_UCSI stopped version=%x buffer=%u cci=%x error=%x failure=%u phase=%u query=%u op=%x control=%llx reply-bytes=%u timeouts=%u; fresh GLINK required\n",
                        ucsi.working.version,ucsi.buffer_bytes,ucsi.cci,ucsi.firmware_error,(unsigned)ucsi.failure,
                        (unsigned)ucsi.failed_phase,ucsi.query,ucsi.last_opcode,ucsi.last_control,ucsi.last_reply_bytes,ucsi.timeouts);
                }
            } else {
                if(ucsi.phase==EldenUcsiReady && (ucsi.refresh_wanted || !ucsiRefresh || now>=ucsiRefresh))elden_ucsi_refresh(&ucsi);
                if(ucsi.polling) {
                    if(!ucsiTick)clock_interval_to_deadline(20,kMillisecondScale,&ucsiTick);
                    if(now>=ucsiTick){elden_ucsi_tick(&ucsi);ucsiTick=0;}
                } else ucsiTick=0;
                uint8_t request[ELDEN_UCSI_MAX_MESSAGE];int bytes=elden_ucsi_next(&ucsi,request,sizeof(request));
                if(bytes>0) {
                    if(sheng_adsp_send_epoch(channel,seen,request,(uint32_t)bytes))elden_ucsi_unsent(&ucsi);
                    else clock_interval_to_deadline(5000,kMillisecondScale,&ucsiDeadline);
                }
            }
        }
        if(policyEnable==1){
            if(chargePolicy.pending&&policyDeadline&&now>=policyDeadline)elden_charge_policy_timeout(&chargePolicy);
            if(chargePolicy.phase==EldenPolicyFailed){
                __atomic_store_n(&policyAvailable,0,__ATOMIC_RELEASE);
                if(!policyReported){
                    report("\nELDEN_CHARGE stopped errors=%u timeouts=%u firmware=%x\n",chargePolicy.errors,chargePolicy.timeouts,chargePolicy.firmware_error);
                    policyReported=true;
                }
            }else{
                if(chargePolicy.original_saved&&chargePolicy.phase!=EldenPolicyReadWindow&&chargePolicy.phase!=EldenPolicyReadEu&&
                   !__atomic_load_n(&policyAvailable,__ATOMIC_ACQUIRE)){
                    __atomic_store_n(&policyAvailable,1,__ATOMIC_RELEASE);
                    report("\nELDEN_CHARGE ready original-window=%u EU-window=%u; stock/bypass/80/40-60\n",chargePolicy.original_window,chargePolicy.eu_window);
                }
                unsigned requested=__atomic_load_n(&policyRequestSequence,__ATOMIC_ACQUIRE);
                if(requested!=policySeenRequest){
                    elden_charge_policy_select(&chargePolicy,__atomic_load_n(&policyRequested,__ATOMIC_ACQUIRE));policySeenRequest=requested;
                }
                if(chargePolicy.applied_sequence!=policySeenApplied){
                    policySeenApplied=chargePolicy.applied_sequence;
                    __atomic_store_n(&policyPublicState,((uint64_t)(++policyApplyCounter)<<32)|chargePolicy.applied_mode,__ATOMIC_RELEASE);
                    report("\nELDEN_CHARGE applied mode=%u window=%u EU-window=%u enabled=%u firmware-held=%u cycle=%u\n",
                        chargePolicy.applied_mode,chargePolicy.window,chargePolicy.eu_window,chargePolicy.observed_enable,
                        chargePolicy.firmware_held?1u:0u,chargePolicy.observed_cycle);
                }
                const bool stable=policyReadyAt&&now>=policyReadyAt&&committedExpiry&&now<committedExpiry&&
                    poll.committed.cycle>=4&&poll.committed.valid==((UINT64_C(1)<<EldenChargeFieldCount)-1)&&notify.registered&&!poll.quarantined;
                const bool allowed=stable&&poll.committed.value[EldenChargePresent].raw==1&&poll.committed.value[EldenChargeHealth].raw==1&&
                    poll.committed.value[EldenChargeVoltage].raw>=3500000&&poll.committed.value[EldenChargeVoltage].raw<=4500000&&
                    poll.committed.value[EldenChargeTemperature].signed_value>=500&&poll.committed.value[EldenChargeTemperature].signed_value<=4200&&
                    poll.committed.value[EldenChargeUsbOnline].raw==1;
                uint8_t request[24];int bytes=stable?elden_charge_policy_next(&chargePolicy,allowed,request):0;
                if(bytes==24){
                    if(sheng_adsp_send_epoch(channel,seen,request,sizeof(request)))elden_charge_policy_unsent(&chargePolicy);
                    else {
                        clock_interval_to_deadline(1000,kMillisecondScale,&policyDeadline);
                        report("\nELDEN_CHARGE request op=%x property=%u value=%u mode=%u\n",
                            chargePolicy.last_opcode,chargePolicy.last_property,chargePolicy.last_value,chargePolicy.active_mode);
                    }
                }
            }
        }
        if(votesEnable==1){
            if(chargeVotes.pending&&votesDeadline&&now>=votesDeadline)elden_charge_votes_timeout(&chargeVotes);
            if(chargeVotes.failed&&!votesReported){
                report("\nELDEN_VOTE stopped index=%u tables=%u errors=%u timeouts=%u\n",
                    chargeVotes.index,chargeVotes.tables,chargeVotes.errors,chargeVotes.timeouts);votesReported=true;
            }
            if(chargeVotes.ready&&now>=votesRefresh)elden_charge_votes_refresh(&chargeVotes);
            const bool stable=votesReadyAt&&now>=votesReadyAt&&poll.committed.cycle>=4&&
                poll.committed.valid==((UINT64_C(1)<<EldenChargeFieldCount)-1)&&notify.registered;
            uint8_t request[16];int bytes=stable&&logsChannel?elden_charge_votes_next(&chargeVotes,request):0;
            if(bytes==16){
                if(sheng_adsp_send_epoch(logsChannel,logsSeen,request,sizeof(request)))elden_charge_votes_unsent(&chargeVotes);
                else {
                    clock_interval_to_deadline(1000,kMillisecondScale,&votesDeadline);
                    if(!chargeVotes.tables)report("\nELDEN_VOTE list request bytes=16; stable epoch=%u PMIC-cycle=%u after 15s\n",chargeVotes.epoch,poll.committed.cycle);
                }
            }
        }
        if(regsEnable){
            if(registers.pending&&regsDeadline&&now>=regsDeadline)elden_power_reg_fail(&registers,EldenPowerRegTimeout);
            if(registers.failed&&!regsReported){
                report("\nELDEN_POWER_REG stopped error=%u index=%u count=%x; fresh logs epoch required\n",registers.error,registers.index,registers.reply_count);
                regsReported=true;
            }
            bool stable=logsChannel&&votesReadyAt&&now>=votesReadyAt&&committedExpiry&&now<committedExpiry&&
                poll.committed.cycle>=4&&poll.committed.valid==((UINT64_C(1)<<EldenChargeFieldCount)-1);
            stable=stable&&!__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE)&&
                elden_power_reg_scheduled(&regsSchedule,bootMode!=0,
                    elden_diagnostic_terminal(&bootstrap,&icl,elden_thermal_pulse_state(),poll.epoch,
                        poll.committed.value[EldenChargeUsbLimit].raw,committedExpiry&&now<committedExpiry,regsEnable==2),
                    registers.cycles,milliseconds(now));
            if(stable&&!registers.active&&!registers.pending&&(!regsRefresh||now>=regsRefresh)){
                if(elden_power_reg_begin(&registers)&&registers.extended)
                    report("\nELDEN_DIAGNOSTIC registers phase=%u error=%u restored=%u; GET only after settled terminal state\n",bootstrap.phase,bootstrap.error,bootstrap.restored);
            }
            uint8_t request[28];int bytes=stable?elden_power_reg_next(&registers,request):0;
            if(bytes==28){
                if(sheng_adsp_send_epoch(logsChannel,logsSeen,request,sizeof(request)))elden_power_reg_unsent(&registers);
                else clock_interval_to_deadline(1000,kMillisecondScale,&regsDeadline);
            }
        }
        if(ulogEnable){
            bool traceStop=__atomic_load_n(&logFilterStop,__ATOMIC_ACQUIRE)||__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE);
            if(ulogEnable!=3&&__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE))elden_ulog_fail(&ulog,EldenUlogStopped);
            if(ulog.pending&&ulogDeadline&&now>=ulogDeadline)elden_ulog_fail(&ulog,EldenUlogTimeout);
            if(ulog.failed&&!ulogReported){
                report("\nELDEN_ULOG stopped error=%u requests=%u completed=%u epoch=%u init-pages=%u current-pages=%u init-empty=%u current-empty=%u\n",
                    ulog.error,ulog.requests,ulog.completed,ulog.epoch,ulog.init_pages,ulog.current_pages,(unsigned)ulog.init_empty,(unsigned)ulog.current_empty);ulogReported=true;
            }
            /* One 64B row per event loop keeps telemetry/control/recovery running. */
            if(ulog.ready){
                if(!ulogOffset)report("\nELDEN_ULOG_BEGIN sequence=%u epoch=%u op=%x bytes=%u crc=%08x\n",
                    ulog.completed,ulog.epoch,ulog.last_opcode,ELDEN_ULOG_PAYLOAD,ulog.crc);
                char hex[129];static const char digits[]="0123456789abcdef";
                for(unsigned i=0;i<64;i++){uint8_t byte=ulog.payload[ulogOffset+i];hex[i*2]=digits[byte>>4];hex[i*2+1]=digits[byte&15];}
                hex[128]=0;
                report("\nELDEN_ULOG_ROW sequence=%u epoch=%u offset=%u data=%s\n",ulog.completed,ulog.epoch,ulogOffset,hex);
                ulogOffset+=64;
                if(ulogOffset==ELDEN_ULOG_PAYLOAD){
                    report("\nELDEN_ULOG_END sequence=%u epoch=%u crc=%08x\n",ulog.completed,ulog.epoch,ulog.crc);
                    elden_ulog_release(&ulog);
                }
            }
            bool stable=logsChannel&&votesReadyAt&&now>=votesReadyAt&&committedExpiry&&now<committedExpiry&&
                poll.committed.cycle>=4&&poll.committed.valid==((UINT64_C(1)<<EldenChargeFieldCount)-1)&&
                !chargeVotes.pending&&!__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE);
            if(ulogEnable==3){
                if(logFilter.pending&&filterDeadline&&now>=filterDeadline)elden_log_filter_fail(&logFilter,4);
                bool ending=traceStop||ulog.failed||(ulog.completed>=ELDEN_ULOG_MAX_PAGES&&!ulog.ready);
                uint8_t configure[32];int n=(!ulog.pending&&!ulog.ready)?elden_log_filter_next(&logFilter,stable,ending,configure):0;
                if(n==32){
                    if(sheng_adsp_send_epoch(logsChannel,logsSeen,configure,sizeof(configure)))elden_log_filter_unsent(&logFilter);
                    else{clock_interval_to_deadline(1000,kMillisecondScale,&filterDeadline);report("\nELDEN_ULOG_FILTER queued phase=%u mask=%x/%x level=%u; diagnostics only\n",logFilter.phase,messageOwner(configure+20),messageOwner(configure+16),messageOwner(configure+24));}
                }
                if(filterSeen!=(unsigned)logFilter.phase){filterSeen=logFilter.phase;report("\nELDEN_ULOG_FILTER phase=%u error=%u requests=%u acks=%u restored=%u\n",filterSeen,logFilter.error,logFilter.requests,logFilter.acks,(unsigned)logFilter.restored);}
                if(logFilter.phase==EldenLogFilterDone||logFilter.phase==EldenLogFilterFailed){
                    __atomic_store_n(&logFilterStopOk,(unsigned)(logFilter.restored||!logFilter.touched),__ATOMIC_RELEASE);
                    __atomic_store_n(&logFilterTerminal,1u,__ATOMIC_RELEASE);
                }
                stable=stable&&!ending&&logFilter.phase==EldenLogFilterActive&&!logFilter.pending&&now>=policyReadAt;
            }else stable=stable&&elden_power_reg_scheduled(&ulogSchedule,bootMode!=0,
                    elden_diagnostic_terminal(&bootstrap,&icl,elden_thermal_pulse_state(),poll.epoch,
                        poll.committed.value[EldenChargeUsbLimit].raw,committedExpiry&&now<committedExpiry,ulogEnable==2),0,milliseconds(now));
            uint8_t request[16];int bytes=stable?elden_ulog_next(&ulog,request):0;
            if(bytes==16){
                if(sheng_adsp_send_epoch(logsChannel,logsSeen,request,sizeof(request)))elden_ulog_unsent(&ulog);
                else {
                    clock_interval_to_deadline(1000,kMillisecondScale,&ulogDeadline);
                    if(ulogEnable==3){clock_interval_to_deadline(5000,kMillisecondScale,&policyReadAt);report("\nELDEN_ULOG_TRACE page=%u uptime-ms=%llu; max12 current pages\n",ulog.requests,milliseconds(now));}
                    else if(ulog.requests==1)report("\nELDEN_DIAGNOSTIC ulog phase=%u error=%u restored=%u; GET only after settled terminal state\n",bootstrap.phase,bootstrap.error,bootstrap.restored);
                }
            }
        }
        if(dcEnable){
            bool stopping=__atomic_load_n(&dcNoopStop,__ATOMIC_ACQUIRE)||__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE);
            if(dcNoop.pending&&dcDeadline&&now>=dcDeadline)elden_dc_noop_fail(&dcNoop,EldenDcNoopTimeout);
            if(stopping)elden_dc_noop_cancel(&dcNoop);
            bool fresh=committedExpiry&&now<committedExpiry&&!poll.quarantined;
            bool ucsiFresh=ucsiCommittedExpiry&&now<ucsiCommittedExpiry&&ucsi.phase!=EldenUcsiFailed;
            EldenPdContract contract={};const auto &p=ucsi.committed.port[1];
            bool qualified=!stopping&&logsChannel&&logsSeen==dcNoop.epoch&&fresh&&ucsiFresh&&ucsi.events_ready&&
                bootstrap.epoch==poll.epoch&&icl.up&&icl.owns_limit&&!icl.pending&&!icl.quarantined&&!icl.suspend_requested&&icl.applied==500000&&
                poll.committed.value[EldenChargeUsbLimit].raw==500000&&!chargePolicy.pending&&!notify.pending&&
                votesCaptured&&milliseconds(now)-milliseconds(votesCaptured)<=3000&&onlySdpInputVote()&&
                elden_thermal_cold_now(poll.epoch)&&
                elden_pd_boot_qualified(&poll.committed,&ucsi.committed,fresh,ucsiFresh,notify.registered&&!notify.quarantined,elden_usb_power_budget_ua())&&
                p.pdos_consistent&&!elden_pd_contract(p.source_pdos,p.pdo_count,p.rdo,&contract)&&contract.type==0&&contract.exact_voltage&&
                contract.requested_mv==9000&&contract.requested_ma>=500&&
                poll.committed.value[EldenChargeWlsOnline].raw==1&&poll.committed.value[EldenChargeWlsVoltage].raw>=8000000&&poll.committed.value[EldenChargeWlsVoltage].raw<=10000000;
            bool scheduled=elden_power_reg_scheduled(&dcSchedule,true,
                bootstrap.phase==EldenPdBootDone&&bootstrap.error==EldenPdBootOk&&bootstrap.restored,0,milliseconds(now));
            if(dcNoop.phase==EldenDcNoopOff&&scheduled&&qualified){
                if(elden_dc_noop_begin(&dcNoop))report("\nELDEN_DC_NOOP start epoch=%u; fixed bus0 SID7 2a52=10 only, budget not increased\n",dcNoop.epoch);
            }
            uint8_t request[28];int bytes=elden_dc_noop_next(&dcNoop,qualified,request);
            if(bytes==28){
                if(sheng_adsp_send_epoch(logsChannel,logsSeen,request,sizeof(request)))elden_dc_noop_unsent(&dcNoop);
                else {
                    clock_interval_to_deadline(1000,kMillisecondScale,&dcDeadline);
                    if(messageOwner(request+8)==0x38)report("\nELDEN_DC_NOOP write queued addr=2a52 raw=10 ma=500; one same-value attempt\n");
                }
            }
            if(dcNoop.phase==EldenDcNoopDone||dcNoop.phase==EldenDcNoopFailed){
                if(!dcReported){
                    report("\nELDEN_DC_NOOP terminal phase=%u error=%u attempted=%u ack=%u verified=%u\n",dcNoop.phase,dcNoop.error,(unsigned)dcNoop.write_attempted,dcNoop.write_acks,(unsigned)dcNoop.verified);dcReported=true;
                }
                __atomic_store_n(&dcNoopStopOk,(unsigned)(dcNoop.verified||!dcNoop.write_attempted),__ATOMIC_RELEASE);
                __atomic_store_n(&dcNoopTerminal,1u,__ATOMIC_RELEASE);
            }
        }
        if(iclEnable==1) {
            if(icl.pending&&iclDeadline&&now>=iclDeadline)elden_usb_icl_timeout(&icl);
            if(icl.quarantined&&!iclReported) {
                report("\nELDEN_USB_ICL stopped epoch=%u timeouts=%u ret=%x; no retry before fresh link\n",
                    icl.epoch,icl.timeouts,icl.ret_code);iclReported=true;
            }
            const bool fresh=committedExpiry&&now<committedExpiry&&!poll.quarantined;
            const uint32_t budget=elden_usb_power_budget_ua();uint8_t request[24];
            if(bootMode){
                bool ucsiFresh=ucsiCommittedExpiry&&now<ucsiCommittedExpiry&&ucsi.phase!=EldenUcsiFailed;
                EldenPdContract contract={};const auto &pd=ucsi.committed.port[1];
                bool high=ucsiFresh&&!elden_pd_contract(pd.source_pdos,pd.pdo_count,pd.rdo,&contract)&&contract.type==0&&contract.exact_voltage&&
                    !contract.capability_mismatch&&!contract.give_back&&contract.requested_ma>=500&&
                    contract.requested_mv>=8000&&contract.requested_mv<=10000&&
                    poll.committed.value[EldenChargeWlsOnline].raw==1&&poll.committed.value[EldenChargeWlsVoltage].raw>=8000000&&
                    poll.committed.value[EldenChargeWlsVoltage].raw<=10000000;
                EldenPdBootInput input={};input.now_ms=milliseconds(now);input.epoch=poll.epoch;input.cycle=poll.committed.cycle;
                input.deadline_seconds=elden_recovery_deadline_remaining?elden_recovery_deadline_remaining():0;
                input.icl_applied=icl.applied;input.icl_observed=poll.committed.value[EldenChargeUsbLimit].raw;
                input.pulse=elden_thermal_pulse_state();input.link_up=poll.up;
                input.qualified=elden_pd_boot_qualified(&poll.committed,&ucsi.committed,fresh,ucsiFresh,notify.registered&&!notify.quarantined,budget);
                input.raw_fresh=fresh;input.high_voltage=high;input.icl_owned=icl.owns_limit;input.icl_pending=icl.pending;
                input.icl_quarantined=icl.quarantined;input.stop=__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE)!=0;
                EldenPdBootAction action;elden_pd_boot_step(&bootstrap,&input,&action);
                if(action.request_pulse&&(__atomic_load_n(&bootstrapStop,__ATOMIC_ACQUIRE)||elden_thermal_pulse_request())){
                    bootstrap.phase=EldenPdBootFailed;bootstrap.error=EldenPdBootPulseFailed;action.cancel_pulse=true;action.suspend_icl=false;
                }
                if(action.cancel_pulse)elden_thermal_pulse_cancel();
                elden_usb_icl_suspend(&icl,action.suspend_icl);
                __atomic_store_n(&bootstrapStopped,(unsigned)(input.stop&&action.stop_ready),__ATOMIC_RELEASE);
                if(bootstrapSeen!=(unsigned)bootstrap.phase){
                    bootstrapSeen=(unsigned)bootstrap.phase;
                    report("\nELDEN_PD_BOOT phase=%u error=%u pulse=%u high=%u cycle=%u deadline=%u suspend=%u restored=%u\n",
                        bootstrapSeen,(unsigned)bootstrap.error,elden_thermal_pulse_state(),high,input.cycle,input.deadline_seconds,action.suspend_icl,bootstrap.restored);
                }
                if(input.cycle!=bootstrapCycle&&bootstrap.phase>=EldenPdBootWait&&bootstrap.phase<=EldenPdBootRestore){
                    bootstrapCycle=input.cycle;
                    report("\nELDEN_PD_GATE cycle=%u phase=%u qualified=%u raw=%u ucsi=%u budget=%u owned=%u pending=%u icl=%u/%u pulse=%u high=%u left=%u\n",
                        input.cycle,(unsigned)bootstrap.phase,input.qualified,fresh,ucsiFresh,budget,icl.owns_limit,icl.pending,
                        icl.applied,input.icl_observed,input.pulse,high,input.deadline_seconds);
                }
            }
            bool writeFresh=fresh&&(!icl.sdp_only||(ucsiCommittedExpiry&&now<ucsiCommittedExpiry&&
                ucsi.phase!=EldenUcsiFailed&&ucsi.events_ready&&!ucsi.active_connector));
            int bytes=elden_usb_icl_next(&icl,&poll.committed,&ucsi.committed,writeFresh,
                notify.registered&&!notify.quarantined,budget,request);
            if(icl.deferred&&poll.committed.cycle!=iclDeferredCycle){
                iclDeferredCycle=poll.committed.cycle;
                report("\nELDEN_USB_HANDOFF deferred cycle=%u fresh=%u adapter=%u real=%u get=%u; no current SET, firmware input ownership\n",
                    iclDeferredCycle,(unsigned)writeFresh,poll.committed.value[EldenChargeUsbAdapter].raw,
                    poll.committed.value[EldenChargeUsbRealType].raw,poll.committed.value[EldenChargeUsbLimit].raw);
            }
            if(bytes==24) {
                if(sheng_adsp_send_epoch(channel,seen,request,sizeof(request)))elden_usb_icl_unsent(&icl);
                else {
                    ++usbIclSends;clock_interval_to_deadline(1000,kMillisecondScale,&iclDeadline);
                    report("\nELDEN_USB_ICL request=%u budget=%u previous-get=%u cycle=%u; SDP-guard=%u, verify firmware GET\n",
                        icl.requested,budget,poll.committed.value[EldenChargeUsbLimit].raw,poll.committed.cycle,(unsigned)icl.sdp_only);
                }
            }
        }
        if(poll.quarantined) {
            __atomic_store_n(&elden_pmic_valid,0u,__ATOMIC_RELEASE);
            if(elden_pmic_stats[0]!=3) {
                elden_pmic_stats[0]=3;elden_pmic_stats[8]=(unsigned)poll.failed_field;
                report("\nELDEN_PMIC stopped field=%d timeout=%u errors=%u; fresh GLINK epoch required\n",
                    poll.failed_field,poll.timeouts,poll.errors);
#if ELDEN_BATTERY_RUNTIME
                elden_battery_runtime_unavailable(poll.epoch,EldenBatteryMissing);
#endif
            }
        } else if(notifyEnable!=1 || notify.quarantined || (notify.registered && now>=notifySettle)) {
            if(poll.up && !poll.active && (!nextRefresh || now>=nextRefresh))elden_charge_poll_refresh(&poll);
            uint8_t request[24];int bytes=elden_charge_poll_next(&poll,request);
            if(bytes==24) {
                if(!cycleStart)cycleStart=now;
                if(sheng_adsp_send_epoch(channel,seen,request,sizeof(request))) elden_charge_poll_unsent(&poll);
                else {clock_interval_to_deadline(1000,kMillisecondScale,&deadline);elden_pmic_stats[0]=2;}
            }
        }
        assert_wait_timeout((event_t)&linkEpoch,THREAD_UNINT,(poll.pending||ucsi.pending||ucsi.polling||notify.pending||icl.pending||chargeVotes.pending||chargePolicy.pending||registers.pending||dcNoop.pending||logFilter.pending||(ulogEnable==3&&ulog.ready))?5:50,kMillisecondScale);
        thread_block(THREAD_CONTINUE_NULL);
    }
}
}
extern "C" int elden_pd_bootstrap_request_stop(void){
    __atomic_store_n(&bootstrapStop,1u,__ATOMIC_RELEASE);elden_thermal_pulse_cancel();
    if(!__atomic_load_n(&bootstrapEnabled,__ATOMIC_ACQUIRE))return 1;
    thread_wakeup((event_t)&linkEpoch);
    for(unsigned i=0;i<500&&!__atomic_load_n(&bootstrapStopped,__ATOMIC_ACQUIRE);i++)IOSleep(10);
    return __atomic_load_n(&bootstrapStopped,__ATOMIC_ACQUIRE)?1:0;
}
extern "C" int elden_dc_noop_request_stop(void){
    __atomic_store_n(&dcNoopStop,1u,__ATOMIC_RELEASE);
    if(!__atomic_load_n(&dcNoopEnabled,__ATOMIC_ACQUIRE))return 1;
    thread_wakeup((event_t)&linkEpoch);
    for(unsigned i=0;i<300&&!__atomic_load_n(&dcNoopTerminal,__ATOMIC_ACQUIRE);i++)IOSleep(10);
    return __atomic_load_n(&dcNoopTerminal,__ATOMIC_ACQUIRE)&&__atomic_load_n(&dcNoopStopOk,__ATOMIC_ACQUIRE);
}
extern "C" int elden_ulog_filter_request_stop(void){
    __atomic_store_n(&logFilterStop,1u,__ATOMIC_RELEASE);
    if(!__atomic_load_n(&logFilterEnabled,__ATOMIC_ACQUIRE))return 1;
    thread_wakeup((event_t)&linkEpoch);
    for(unsigned i=0;i<300&&!__atomic_load_n(&logFilterTerminal,__ATOMIC_ACQUIRE);i++)IOSleep(10);
    return __atomic_load_n(&logFilterTerminal,__ATOMIC_ACQUIRE)&&__atomic_load_n(&logFilterStopOk,__ATOMIC_ACQUIRE);
}
extern "C" void elden_pmic_client_start(void) {
    unsigned enable=0,adsp=0,ucsi=0;
    PE_parse_boot_argn("elden-pmic",&enable,sizeof(enable));PE_parse_boot_argn("elden-adsp",&adsp,sizeof(adsp));
    PE_parse_boot_argn("elden-ucsi-query",&ucsi,sizeof(ucsi));
    if(enable!=1 || adsp!=2 || ucsi>1 || __atomic_exchange_n(&attempted,1u,__ATOMIC_ACQ_REL))return;
    lock=IOLockAlloc();thread_t thread=nullptr;
    if(!lock || kernel_thread_start(worker,nullptr,&thread)!=KERN_SUCCESS) {
        report("\nELDEN_PMIC refused worker allocation\n");return;
    }
    thread_deallocate(thread);
}
extern "C" int elden_charge_policy_request(unsigned mode){
    if(mode<EldenChargeStock||mode>EldenChargeWindow4060)return -1;
    if(!__atomic_load_n(&policyAvailable,__ATOMIC_ACQUIRE))return -2;
    __atomic_store_n(&policyRequested,mode,__ATOMIC_RELEASE);
    __atomic_add_fetch(&policyRequestSequence,1,__ATOMIC_RELEASE);
    thread_wakeup((event_t)&linkEpoch);return 0;
}
extern "C" unsigned elden_charge_policy_ready(void){return __atomic_load_n(&policyAvailable,__ATOMIC_ACQUIRE);}
extern "C" uint64_t elden_charge_policy_public_state(void){return __atomic_load_n(&policyPublicState,__ATOMIC_ACQUIRE);}
