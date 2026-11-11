/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "DcNoop.h"
#include "NativeBackend.h"
#include <stdarg.h>
extern "C" void elden_usb_log_putc(char);
namespace {
void report(const char *format,...){
    char text[256];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    for(const char *p=text;*p;p++)elden_usb_log_putc(*p);
}
}
extern "C" void elden_dc_spmi_survey(void){
    using elden_thermal_native::Region;
    Region core,owners;
    if(!core.map(ELDEN_ADC_SPMI_CORE,ELDEN_ADC_SPMI_CORE_BYTES)||
       !owners.map(ELDEN_ADC_SPMI_OWNER,ELDEN_ADC_SPMI_OWNER_BYTES)){
        report("\nELDEN_DC_SPMI survey mapping failed; no bus commands\n");return;
    }
    const uint32_t version=core.read(0),count=core.read(4)&0x7ff;
    if(version!=ELDEN_ADC_SPMI_VERSION||count!=ELDEN_ADC_SPMI_APIDS){
        report("\nELDEN_DC_SPMI survey refused version=%x count=%u\n",version,count);return;
    }
    const unsigned targets[]={0x727,0x729,0x72a};unsigned matches[3]={},owned[3]={};
    for(unsigned i=0;i<ELDEN_ADC_SPMI_APIDS;i++){
        uint32_t row=core.read(0x2000+4*i);if(!row)continue;
        unsigned ppid=(row>>8)&0xfff;
        for(unsigned j=0;j<3;j++)if(ppid==targets[j]){
            uint32_t owner=owners.read(4*i);++matches[j];if((owner&7)==ELDEN_ADC_SPMI_EE)++owned[j];
            report("\nELDEN_DC_SPMI ppid=%x apid=%u owner=%u row=%x owner-word=%x; observation only\n",ppid,i,owner&7,row,owner);
        }
    }
    for(unsigned j=0;j<3;j++)report("\nELDEN_DC_SPMI summary ppid=%x matches=%u AP-owned=%u; no observer/writer mapped or commanded\n",targets[j],matches[j],owned[j]);
}
