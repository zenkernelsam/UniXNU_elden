/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Full monitor is explicit. GPIO probing precedes ADC activity; no output is
 * changed before qualified readings arm the state machine. */
#include "NativeBackend.h"
#include "Pins.h"
#include "Control.h"
#include "BatteryRuntime.h"
#include "PulseControl.h"
#include "ColdToken.h"
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>
extern "C" {
void elden_usb_log_putc(char);
__attribute__((weak)) volatile uint32_t sm8750_adc_owner;
unsigned elden_recovery_deadline_remaining(void) __attribute__((weak_import));
}
namespace {
using namespace elden_thermal_native;
constexpr uint32_t ProtectionOwner=0x54484d35;
static volatile unsigned running,stopRequested;
static volatile unsigned pulseState;
static volatile uint64_t coldToken;
void report(const char *format,...){
    char text[352];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);
    for(const char *p=text;*p;p++)elden_usb_log_putc(*p);
}
struct Protection {
    static void *operator new(size_t n) noexcept{return IOMalloc(n);}
    static void operator delete(void *p) noexcept{if(p)IOFree(p,sizeof(Protection));}
    SpmiEndpoint converter,gpio;Region tlmm;
    EldenAdc adc={};EldenThermalPins pins={};EldenThermalControl control={};
    bool converterOpened=false,pulseHeld=false;
};
uint32_t tlmmRead(void *p,uint32_t r){return static_cast<Protection *>(p)->tlmm.read(r);}
int tlmmWrite(void *p,uint32_t r,uint32_t v){if(r!=4)return -22;static_cast<Protection *>(p)->tlmm.write(r,v);return 0;}
int gpioRead(void *p,uint8_t r,uint8_t *v){return elden_adc_spmi_read(&static_cast<Protection *>(p)->gpio.bus,r,v,1);}
int gpioWrite(void *p,uint8_t r,uint8_t v){return elden_adc_spmi_write(&static_cast<Protection *>(p)->gpio.bus,r,&v,1);}
void dispose(Protection *p,int rc){
    __atomic_store_n(&coldToken,UINT64_C(0),__ATOMIC_RELEASE);
    int release=p->converterOpened?elden_adc_release(&p->adc):0;
    report("\nELDEN_THERMAL end result=%d adc-release=%d adc-uncertain=%u gpio-uncertain=%u writes=%u/%u armed=%u; output levels retained\n",
        rc,release,p->converter.bus.uncertain,p->gpio.bus.uncertain,p->pins.writes[0],p->pins.writes[1],p->control.armed);
    if(!release&&!p->converter.bus.uncertain&&!p->gpio.bus.uncertain&&!p->pulseHeld&&!p->pins.failed[0]&&!p->pins.failed[1]){
        delete p;uint32_t owner=ProtectionOwner;
        __atomic_compare_exchange_n(&sm8750_adc_owner,&owner,0u,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    }
    /* Never restore an old low level while leaving a hot/faulted monitor. */
}
int closeInputs(Protection *p){
    int first=elden_thermal_pin_set(&p->pins,1,1),second=elden_thermal_pin_set(&p->pins,2,1);
    report("\nELDEN_THERMAL fail-closed port1=%d port2=%d\n",first,second);return first?first:second;
}
bool pause(){for(unsigned n=0;n<50&&!__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE);n++)IOSleep(100);return !__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE);}
bool pulseCold(const EldenThermalInput &in,const EldenThermalAction &action){
    return action.armed&&!action.fault_latched&&action.level[0]==0&&action.level[1]==0&&in.battery_valid&&
        in.connector_valid[0]&&in.connector_valid[1]&&in.connector_mc[0]>=10000&&in.connector_mc[0]<40000&&
        in.connector_mc[1]>=10000&&in.connector_mc[1]<40000&&in.battery_centi_c>=1000&&in.battery_centi_c<4000;
}
int pulse(Protection *p,const EldenThermalInput &in,const EldenThermalAction &action){
    bool cold=pulseCold(in,action);unsigned off=EldenPulseOff;
    if(cold)__atomic_compare_exchange_n(&pulseState,&off,(unsigned)EldenPulseReady,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&pulseState,__ATOMIC_ACQUIRE)!=EldenPulseQueued)return 0;
    EldenThermalPinView view={};
    if(!cold||__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE)||!elden_recovery_deadline_remaining||
       elden_recovery_deadline_remaining()<90||elden_thermal_pins_refresh(&p->pins,&view)||view.level[0]||view.level[1]){
        __atomic_store_n(&pulseState,(unsigned)EldenPulseRefused,__ATOMIC_RELEASE);
        report("\nELDEN_PD_PULSE refused fresh thermal/pin/deadline gate\n");return 0;
    }
    unsigned queued=EldenPulseQueued;
    if(!__atomic_compare_exchange_n(&pulseState,&queued,(unsigned)EldenPulseHigh,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
    uint64_t start=nowUs(nullptr);p->pulseHeld=true;
    int rc=elden_thermal_pin_set(&p->pins,1,1);
    report("\nELDEN_PD_PULSE high result=%d at-us=%llu; USB may disconnect\n",rc,start);
    if(!rc){
        IOSleep(100);
        /* The grant was made using both fresh ADC readings immediately above.
         * Bound its lifetime; never reopen using an indefinitely stale grant. */
        if(nowUs(nullptr)-start>1000000)rc=EldenAdcFault;
        else rc=elden_thermal_pin_set(&p->pins,1,0);
    }
    if(!rc)p->pulseHeld=false;
    __atomic_store_n(&pulseState,(unsigned)(rc?EldenPulseUncertain:EldenPulseRestored),__ATOMIC_RELEASE);
    report("\nELDEN_PD_PULSE low result=%d elapsed-us=%llu held=%u\n",rc,nowUs(nullptr)-start,p->pulseHeld);
    return rc;
}
void run(unsigned mode){
    if(__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE))return;
    uint32_t empty=0;
    if(!__atomic_compare_exchange_n(&sm8750_adc_owner,&empty,ProtectionOwner,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        report("\nELDEN_THERMAL refused ADC/thermal owner busy\n");return;
    }
    Protection *p=new Protection;
    if(!p){__atomic_store_n(&sm8750_adc_owner,0u,__ATOMIC_RELEASE);return;}
    int rc=p->gpio.open(EldenSpmiUsb2Gpio,mode>=2);
    report("\nELDEN_THERMAL GPIO bus result=%d apid=%u owner=%u target=0188\n",rc,p->gpio.bus.apid,p->gpio.bus.write_owner);
    if(rc||!p->tlmm.map(ELDEN_THERMAL_TLMM_GPIO3,4096)){dispose(p,rc?rc:EldenAdcIoError);return;}
    p->pins.io={p,tlmmRead,tlmmWrite,gpioRead,gpioWrite};EldenThermalPinView view={};
    rc=elden_thermal_pins_probe(&p->pins,&view);
    report("\nELDEN_THERMAL GPIO probe=%d tlmm=%x/%x type=%02x subtype=%02x mode=%02x vin=%02x pull=%02x src=%02x drive=%02x en=%02x levels=%d/%d\n",
        rc,view.tlmm_config,view.tlmm_io,view.type,view.subtype,view.mode,view.vin,view.pull,view.source,view.drive,view.enable,view.level[0],view.level[1]);
    if(rc||mode==1){dispose(p,rc);return;}
    uint64_t hz;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(hz));
    if(hz!=19200000){dispose(p,EldenAdcFault);return;}
    rc=p->converter.open(EldenSpmiAdcSdam,true);if(rc){dispose(p,rc);return;}
    p->adc.io={&p->converter.bus,elden_adc_spmi_read,elden_adc_spmi_write,nowUs,delayUs};
    p->converterOpened=true;rc=elden_adc_acquire(&p->adc);if(rc){dispose(p,rc);return;}
    for(uint32_t sequence=1;!__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE);sequence++){
        __atomic_store_n(&coldToken,UINT64_C(0),__ATOMIC_RELEASE);
        rc=elden_thermal_pins_refresh(&p->pins,&view);
        if(rc){if(p->control.armed)closeInputs(p);break;}
        EldenThermalInput input={};input.sequence=sequence;
        for(unsigned i=0;i<2;i++){
            uint16_t raw=0;int mc=0;
            rc=elden_adc_sample(&p->adc,i+1,&raw,&mc);input.connector_valid[i]=!rc;input.connector_mc[i]=mc;
            report("\nELDEN_THERMAL sample=%u port=%u result=%d raw=%04x mc=%d\n",sequence,i+1,rc,raw,mc);
            if(rc)break;
        }
        uint32_t epoch=0,cycle=0;
        input.battery_valid=elden_battery_temperature_get(&input.battery_centi_c,&epoch,&cycle);
        EldenThermalAction action;elden_thermal_control_step(&p->control,&input,&action);
        report("\nELDEN_THERMAL policy seq=%u armed=%u fault=%u battery=%u epoch=%u cycle=%u temp=%d desired=%d/%d\n",
            sequence,action.armed,action.fault_latched,input.battery_valid,epoch,cycle,input.battery_centi_c,action.level[0],action.level[1]);
        int one=0,two=0;
        if(action.level[0]>=0)one=elden_thermal_pin_set(&p->pins,1,action.level[0]);
        if(action.level[1]>=0)two=elden_thermal_pin_set(&p->pins,2,action.level[1]);
        if(one||two){closeInputs(p);rc=one?one:two;break;}
        if(mode==3&&!rc){int status=pulse(p,input,action);if(status){rc=status;break;}}
        if(!rc&&pulseCold(input,action)&&!p->pulseHeld&&!__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE)){
            uint64_t token=elden_cold_token(epoch,(uint32_t)(nowUs(nullptr)/1000));
            __atomic_store_n(&coldToken,token,__ATOMIC_RELEASE);
        }
        if(rc){
            /* Only a completed out-of-range conversion may be sampled again.
             * Unknown bus/ADC completion never retries or reopens an input. */
            if(rc!=EldenAdcRange||p->adc.quarantined||p->adc.inflight||p->converter.bus.uncertain)break;
            p->adc.failed=false;p->adc.error=0;
        }
        if(!p->control.armed&&sequence>=12){report("\nELDEN_THERMAL refused readiness timeout\n");break;}
        if(!pause())break;
    }
    dispose(p,rc);
}
void worker(void *p,wait_result_t){run((unsigned)(uintptr_t)p);elden_thermal_pulse_cancel();__atomic_store_n(&running,2u,__ATOMIC_RELEASE);thread_terminate(current_thread());}
}
extern "C" void elden_thermal_runtime_start(void){
    unsigned mode=0;PE_parse_boot_argn("elden-thermal",&mode,sizeof(mode));
    if(mode<1||mode>3||!elden_thermal_worker_claim(&running))return;
    thread_t t=nullptr;
    if(kernel_thread_start(worker,(void *)(uintptr_t)mode,&t)==KERN_SUCCESS)thread_deallocate(t);
    else{__atomic_store_n(&running,2u,__ATOMIC_RELEASE);report("\nELDEN_THERMAL refused worker allocation\n");}
}
extern "C" unsigned elden_thermal_pulse_state(void){return __atomic_load_n(&pulseState,__ATOMIC_ACQUIRE);}
extern "C" int elden_thermal_cold_now(uint32_t epoch){
    if(!epoch||__atomic_load_n(&running,__ATOMIC_ACQUIRE)!=1||__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE))return 0;
    uint64_t token=__atomic_load_n(&coldToken,__ATOMIC_ACQUIRE);
    return elden_cold_token_valid(token,epoch,(uint32_t)(nowUs(nullptr)/1000));
}
extern "C" int elden_thermal_pulse_request(void){
    if(__atomic_load_n(&running,__ATOMIC_ACQUIRE)!=1||__atomic_load_n(&stopRequested,__ATOMIC_ACQUIRE))return -1;
    unsigned expected=EldenPulseReady;
    return __atomic_compare_exchange_n(&pulseState,&expected,(unsigned)EldenPulseQueued,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)?0:-2;
}
extern "C" void elden_thermal_pulse_cancel(void){
    unsigned expected=EldenPulseQueued;
    __atomic_compare_exchange_n(&pulseState,&expected,(unsigned)EldenPulseRefused,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
extern "C" int elden_thermal_request_stop(void){
    __atomic_store_n(&stopRequested,1u,__ATOMIC_RELEASE);
    __atomic_store_n(&coldToken,UINT64_C(0),__ATOMIC_RELEASE);
    elden_thermal_worker_close_idle(&running);
    elden_thermal_pulse_cancel();
    for(unsigned i=0;i<300&&__atomic_load_n(&running,__ATOMIC_ACQUIRE)==1;i++)IOSleep(10);
    return __atomic_load_n(&running,__ATOMIC_ACQUIRE)==1?0:1;
}
