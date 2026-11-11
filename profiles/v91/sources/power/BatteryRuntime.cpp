/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* IOPMPowerSource lifecycle follows sheng; all transport/property IDs are Elden's.
 * One writer is the PMIC worker, never the DSP receive callback. Charger control
 * remains firmware-owned. Defaults never publish unqualified or zero-filled SOC.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "BatterySnapshot.h"
#include "BatteryRuntime.h"

extern kmod_info_t g_kernel_kmod_info;
extern "C" void elden_battery_static_init(void) __asm__("__GLOBAL__sub_I_BatteryRuntime.cpp");
extern "C" void elden_usb_log_putc(char);
extern "C" {volatile int32_t elden_battery_stats[10];}
namespace {
EldenBatteryContract contract;
EldenBatteryGate batteryReadiness;
IOSimpleLock *temperatureLock;
struct {int centi;uint32_t epoch,cycle;uint64_t deadline;} temperatureCache;
bool configured;
void report(const char *text) {for(const char *p=text;*p;++p)elden_usb_log_putc(*p);}
void cacheTemperature(const EldenBatterySnapshot *s,uint64_t age_ms){
    if(!temperatureLock)return;
    uint64_t deadline=0;
    if(s&&age_ms<=10000)clock_interval_to_deadline((uint32_t)(10000-age_ms),kMillisecondScale,&deadline);
    IOSimpleLockLock(temperatureLock);
    if(s){temperatureCache.centi=s->temperature_centi_c;temperatureCache.epoch=s->epoch;temperatureCache.cycle=s->cycle;}
    temperatureCache.deadline=deadline;
    IOSimpleLockUnlock(temperatureLock);
}
}
class EldenBattery : public IOPMPowerSource {
    OSDeclareDefaultStructors(EldenBattery)
    bool published=false;
    void number(const char *key,int64_t value,unsigned bits=32) {
        const OSSymbol *k=OSSymbol::withCString(key);
        OSNumber *v=OSNumber::withNumber((unsigned long long)value,bits);
        if(k && v)setPSProperty(k,v);
        OSSafeReleaseNULL(v);OSSafeReleaseNULL(k);
    }
    void flag(const char *key,bool value) {
        const OSSymbol *k=OSSymbol::withCString(key);
        if(k)setPSProperty(k,value?kOSBooleanTrue:kOSBooleanFalse);
        OSSafeReleaseNULL(k);
    }
    void remove(const char *key) {
        if(properties)properties->removeObject(key);
        removeProperty(key);settingsChangedSinceUpdate=true;
    }
    void optional(const char *key,bool known,int value) {
        if(known)number(key,value);else remove(key);
    }
public:
    void publish(const EldenBatterySnapshot &s,const EldenChargeRawSnapshot &raw,uint64_t ageMs) {
        setBatteryInstalled(s.present);setExternalConnected(s.external);
        setExternalChargeCapable(s.external_charge_capable);
        setIsCharging(s.charging);setAtWarnLevel(s.warn);setAtCriticalLevel(s.critical);
        /* Match sheng's percent/100 convention; raw mAh remain separate keys. */
        setCurrentCapacity((unsigned)s.percent);setMaxCapacity(100);
        setVoltage((unsigned)s.voltage_mv);setAmperage(s.current_ma);
        setTimeRemaining(s.optional&EldenBatteryTimeKnown?s.time_remaining_min:0xffff);
        optional("CycleCount",s.optional&EldenBatteryCyclesKnown,s.cycles);
        number("Temperature",s.temperature_centi_c);number("InstantAmperage",s.current_ma);
        optional("AppleRawCurrentCapacity",s.optional&EldenBatteryCounterKnown,s.remaining_mah);
        optional("AppleRawMaxCapacity",s.optional&EldenBatteryFullKnown,s.full_mah);
        optional("NominalChargeCapacity",s.optional&EldenBatteryFullKnown,s.full_mah);
        optional("DesignCapacity",s.optional&EldenBatteryDesignKnown,s.design_mah);
        flag("FullyCharged",s.fully_charged);flag("BatteryDataValid",true);
        remove("ErrorCondition");remove("EldenBatteryFailure");remove("EldenBatteryFailedEpoch");
        number("EldenBatteryEpoch",s.epoch);number("EldenBatteryCycle",s.cycle);
        number("EldenBatteryUnitsProfile",contract.profile);
        number("EldenBatterySampleAgeMs",(int64_t)ageMs,64);
        number("EldenBatteryCurrentMultiplier",contract.current_multiplier);
        /* Independent gauge/USB/WLS raw scalars retain their validity mask.
         * No cell average, per-port label, input sum or invented AdapterDetails. */
        OSData *data=OSData::withBytes(&raw,sizeof(raw));
        const OSSymbol *key=OSSymbol::withCString("EldenPmicRawSnapshot");
        if(data && key)setPSProperty(key,data);
        OSSafeReleaseNULL(data);OSSafeReleaseNULL(key);
        updateStatus();
        if(!published){published=true;registerService();}
    }
    void unavailable(uint32_t epoch,int reason) {
        if(!published)return;
        /* powerd may not understand our validity key. Withdraw charge/current
         * confirmation as well, while retaining the last measured capacity. */
        setIsCharging(false);setExternalChargeCapable(false);
        setAtWarnLevel(false);setAtCriticalLevel(false);setTimeRemaining(0xffff);
        remove("Amperage");remove("InstantAmperage");
        flag("BatteryDataValid",false);number("EldenBatteryFailure",reason);
        number("EldenBatteryFailedEpoch",epoch);
        const OSSymbol *text=OSSymbol::withCString("EldenBatteryTelemetryUnavailable");
        if(text)setErrorCondition(const_cast<OSSymbol *>(text));
        OSSafeReleaseNULL(text);
        /* Last qualified capacity stays labelled unavailable; never report
         * removal, empty SOC or a fabricated cable state on a DSP link loss. */
        updateStatus();
    }
};
OSDefineMetaClassAndStructors(EldenBattery,IOPMPowerSource)
namespace {EldenBattery *battery;}
extern "C" void elden_battery_runtime_configure(void) {
    if(configured)return;configured=true;
    unsigned enable=0,profile=0;int multiplier=0;
    PE_parse_boot_argn("elden-battery",&enable,sizeof(enable));
    PE_parse_boot_argn("elden-battery-profile",&profile,sizeof(profile));
    PE_parse_boot_argn("elden-battery-current",&multiplier,sizeof(multiplier));
    /* This is the already-qualified board's raw contract. Headless thermal
     * protection consumes the same gate; enabling UI remains an explicit flag. */
    contract={1,-1,10000};temperatureLock=IOSimpleLockAlloc();
    if(enable!=1)return;
    if(profile!=1 || multiplier!=-1) {
        elden_battery_stats[0]=-2;report("\nELDEN_BATTERY refused unqualified OEM units/current profile\n");return;
    }
    auto existing=IOService::copyMatchingService(IOService::serviceMatching("IOPMPowerSource"));
    if(existing){existing->release();elden_battery_stats[0]=-3;report("\nELDEN_BATTERY refused existing power source\n");return;}
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle){elden_battery_stats[0]=-4;return;}
    elden_battery_static_init();
    if(OSMetaClass::postModLoad(handle)){elden_battery_stats[0]=-5;return;}
    battery=OSTypeAlloc(EldenBattery);
    if(!battery || !battery->init() || !battery->attach(IOService::getPlatform())) {
        if(battery){battery->release();battery=nullptr;}
        elden_battery_stats[0]=-6;return;
    }
    /* A retained boot-lifetime instance; all updates use the same PMIC worker. */
    elden_battery_stats[0]=1;
    report("\nELDEN_BATTERY ready; registration waits for complete qualified readings; charger SET=0\n");
}
extern "C" void elden_battery_runtime_update(const EldenChargeRawSnapshot *raw,uint64_t age_ms) {
    if(!configured || !raw)return;
    EldenBatterySnapshot s;int rc=elden_battery_gate_update(&batteryReadiness,raw,&contract,age_ms,&s);
    if(rc){
        /* Preserve gate progress: link-loss handling below resets it, whereas
         * a first coherent sample needs the following cycle to confirm it. */
        cacheTemperature(nullptr,0);
        if(battery)battery->unavailable(raw->epoch,rc);
        elden_battery_stats[0]=3;elden_battery_stats[7]=rc;
        char text[176];snprintf(text,sizeof(text),"\nELDEN_BATTERY held epoch=%u cycle=%u reason=%d confirmations=%u\n",raw->epoch,raw->cycle,rc,batteryReadiness.confirmations);
        report(text);return;
    }
    cacheTemperature(&s,age_ms);
    bool firstDataOnly=!battery&&elden_battery_stats[0]!=4;
    if(battery)battery->publish(s,*raw,age_ms);
    elden_battery_stats[0]=battery?2:4;
    elden_battery_stats[1]=s.percent;elden_battery_stats[2]=s.voltage_mv;
    elden_battery_stats[3]=s.current_ma;elden_battery_stats[4]=s.temperature_centi_c;
    elden_battery_stats[5]=(int32_t)s.epoch;elden_battery_stats[6]=(int32_t)s.cycle;
    if(!battery){if(firstDataOnly)report("\nELDEN_BATTERY qualified data-only; UI disabled\n");return;}
    char text[240];snprintf(text,sizeof(text),
        "\nELDEN_BATTERY published cycle=%u percent=%d mv=%d ma=%d temp-centi-C=%d external=%u net-charging=%u raw-gauges=independent writes=0\n",
        s.cycle,s.percent,s.voltage_mv,s.current_ma,s.temperature_centi_c,(unsigned)s.external,(unsigned)s.charging);
    report(text);
}
extern "C" void elden_battery_runtime_unavailable(uint32_t epoch,int reason) {
    elden_battery_gate_reset(&batteryReadiness);
    cacheTemperature(nullptr,0);
    if(battery)battery->unavailable(epoch,reason);
    if(battery)elden_battery_stats[0]=3;
    elden_battery_stats[7]=reason;
}
extern "C" bool elden_battery_temperature_get(int *centi,uint32_t *epoch,uint32_t *cycle){
    if(!centi||!epoch||!cycle||!temperatureLock)return false;
    IOSimpleLockLock(temperatureLock);
    bool valid=temperatureCache.deadline&&mach_absolute_time()<temperatureCache.deadline;
    if(valid){*centi=temperatureCache.centi;*epoch=temperatureCache.epoch;*cycle=temperatureCache.cycle;}
    IOSimpleLockUnlock(temperatureLock);return valid;
}
