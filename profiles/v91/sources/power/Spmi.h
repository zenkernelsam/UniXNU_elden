/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Adc.h"
enum EldenAdcSpmiSpace {EldenAdcSpmiCore,EldenAdcSpmiOwner,EldenAdcSpmiObserver,EldenAdcSpmiWriter};
enum EldenThermalSpmiTarget {EldenSpmiAdcSdam=0,EldenSpmiUsb2Gpio=1};
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    uint32_t (*read32)(void *,enum EldenAdcSpmiSpace,uint32_t);
    void (*write32)(void *,enum EldenAdcSpmiSpace,uint32_t,uint32_t);
    void (*delay_us)(void *,unsigned);
} EldenAdcSpmiIo;
typedef struct {
    EldenAdcSpmiIo io;
    enum EldenThermalSpmiTarget target;
    unsigned apid,write_owner,matches,owned_matches,commands;
    uint32_t last_status;
    bool selected,writable,uncertain;
} EldenAdcSpmi;
/* Controller/owner windows precede selection. Caller then maps only the
 * selected observer/writer pages using the v7 offsets. No APID reassignment. */
int elden_adc_spmi_find(EldenAdcSpmi *,bool writable);
int elden_adc_spmi_read(void *,uint8_t,uint8_t *,size_t);
int elden_adc_spmi_write(void *,uint8_t,const uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
