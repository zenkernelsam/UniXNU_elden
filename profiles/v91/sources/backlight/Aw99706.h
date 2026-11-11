/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    int (*read)(void *context,uint8_t reg,uint8_t *value);
    int (*write)(void *context,uint8_t reg,uint8_t value);
} EldenBacklightIo;
typedef struct {
    EldenBacklightIo io;
    bool ready,failed,enabled;
    uint16_t level;
    uint8_t cfg4_reserved,led_config;
    uint8_t observed_id,observed_mode,observed_hi,observed_lo,observed_config;
    uint8_t observed_open,observed_short,observed_flags,observed_cfg7;
    uint32_t writes;
    int error;
} EldenAw99706;
enum {EldenBlOk=0,EldenBlArgument=-1,EldenBlIo=-2,EldenBlIdentity=-3,
      EldenBlConfiguration=-4,EldenBlFault=-5,EldenBlVerify=-6};
/* Caller owns a powered, idle I2C controller and the chip's HWEN epoch.
 * Inspect requires the already-initialized board configuration. No reset,
 * rail/boost/current configuration or MTP register is touched. */
int elden_aw99706_inspect(EldenAw99706 *state,const EldenBacklightIo *io);
int elden_aw99706_set_level(EldenAw99706 *state,unsigned level);
int elden_aw99706_check_health(EldenAw99706 *state);
#ifdef __cplusplus
}
#endif
