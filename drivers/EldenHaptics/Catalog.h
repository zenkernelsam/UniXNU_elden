/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_CATALOG_H
#define ELDEN_HAPTICS_CATALOG_H
#include "RtpStream.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t id,frames;uint8_t mask,gain[2];
    const int8_t *pcm[2];
    const char *name,*source_sha256,*bank_sha256;
} EldenHapticsEffect;
/* Data and limits are compiled immutable after host measurement/evidence/hash
 * qualification. The C validator confirms structural/bounds identity only;
 * it does not certify firmware, gain, temperature, waveform or bus throughput. */
int elden_haptics_catalog_check(const EldenHapticsEffect *,size_t count,const EldenRtpLimits *);
const EldenHapticsEffect *elden_haptics_catalog_find(const EldenHapticsEffect *,size_t count,uint32_t id);
#ifdef __cplusplus
}
#endif
#endif
