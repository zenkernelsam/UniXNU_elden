/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_RTC_SPMI_H
#define ELDEN_RTC_SPMI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRtcSpmiCore,EldenRtcSpmiOwner,EldenRtcSpmiObserver};
typedef struct {
    void *context;
    int (*read32)(void *,unsigned window,uint32_t reg,uint32_t *);
    int (*write32)(void *,unsigned window,uint32_t reg,uint32_t);
    void (*delay_us)(void *,unsigned);
} EldenRtcSpmiIo;
typedef struct {EldenRtcSpmiIo io;uint32_t apid,owner,commands;uint8_t selected,uncertain,reserved[2];} EldenRtcSpmi;
/* Select the unique RTC PPID061 under actual v7.2 bus0/EE0 map. No APID/owner
 * reassignment. A read uses only its observer channel and the shared gate. */
int elden_rtc_spmi_select(EldenRtcSpmi *);
int elden_rtc_spmi_read(void *,uint16_t address,uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
#endif
