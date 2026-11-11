/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenAdcArgument=-22,EldenAdcBusy=-16,EldenAdcIoError=-5,
      EldenAdcTimeout=-110,EldenAdcRange=-34,EldenAdcFault=-71};
enum {EldenAdcHs=0x45,EldenAdcStatus=0x46,EldenAdcEoc=0x4a,EldenAdcEocClear=0x4b,
      EldenAdcConfig=0x4f,EldenAdcChannel=0x50,EldenAdcData=0x5c,EldenAdcRequest=0xe5};
typedef struct {
    void *context;
    int (*read)(void *,uint8_t offset,uint8_t *bytes,size_t count);
    int (*write)(void *,uint8_t offset,const uint8_t *bytes,size_t count);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,unsigned);
} EldenAdcIo;
typedef struct {
    uint8_t handshake,status,eoc,request,config[7];
    uint16_t previous_code;
} EldenAdcView;
typedef struct {
    EldenAdcIo io;EldenAdcView initial;
    uint32_t samples;
    uint16_t raw;
    unsigned stage;
    int error;
    bool acquired,dirty,inflight,failed,quarantined;
} EldenAdc;
/* Read-only SDAM inspection. The caller owns its SPMI observer channel. */
int elden_adc_inspect(EldenAdc *,EldenAdcView *);
/* Exclusive use of immediate slot0; leave TM channels, calibration and
 * threshold registers alone. Unknown completion keeps the lease until reset. */
int elden_adc_acquire(EldenAdc *);
int elden_adc_sample(EldenAdc *,unsigned usb_port,uint16_t *raw,int *temperature_mc);
int elden_adc_release(EldenAdc *);
/* Exact stock ratiometric scale and the unmodified Armada/Qualcomm NTC table. */
int elden_adc_therm_scale(unsigned usb_port,uint16_t raw,int *temperature_mc);
#ifdef __cplusplus
}
#endif
