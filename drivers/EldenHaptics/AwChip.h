/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_AW_CHIP_H
#define ELDEN_AW_CHIP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum EldenAwOperation {EldenAwInspect=0,EldenAwConfigure=1,EldenAwPlay=2,EldenAwStop=3};
enum {EldenAwOk=0,EldenAwArgument=-22,EldenAwNoDevice=-19,EldenAwBusy=-16,
      EldenAwTimeout=-110,EldenAwIntegrity=-84,EldenAwCancelled=-125};
typedef struct {
    void *context;
    /* One shared native bus owner provides complete, bounded I2C transfers.
     * read/write preserve the no-increment data-port semantics of reg42.
     * admit is checked before EVERY transaction/delay. A stop may use only
     * its original bounded cleanup lease. These callbacks must never reenter.
     * raw firmware identity MUST be checked by the provider, not a UI flag. */
    int (*read)(void *,uint8_t address,uint8_t reg,uint8_t *,size_t);
    int (*write)(void *,uint8_t address,uint8_t reg,const uint8_t *,size_t);
    int (*admit)(void *,uint8_t address,enum EldenAwOperation);
    int (*firmware)(void *,const uint8_t *,size_t);
    uint64_t (*now_us)(void *);
    int (*delay_us)(void *,unsigned low,unsigned high);
} EldenAwIo;
typedef struct {
    EldenAwIo io;
    const uint8_t *image;
    size_t image_bytes;
    uint16_t base,first,end;
    uint8_t address,waves,ready,held,cleanup,busy;
    uint8_t max_gain,active_wave,active_gain,inspecting;
    uint8_t starting,play_mode,reserved[2];
    uint32_t requested_duration;
    uint64_t deadline,setup_deadline,last_clock;
    int sticky_error;
    uint32_t reads,writes,starts,completions,stops;
} EldenAwChip;
typedef struct {
    uint16_t base,first,end;
    uint8_t waves;
} EldenAwImage;
/* Pure validation: image's own checksum/directory, not its signed identity.
 * Exact raw1436B firmware hash remains an external/native firmware prerequisite. */
int elden_aw_image(const uint8_t *,size_t,EldenAwImage *);
int elden_aw_bind(EldenAwChip *,const EldenAwIo *,uint8_t address);
int elden_aw_identity(EldenAwChip *,uint16_t *);
/* Bound/native calibration supplies max_gain; zero forbids playback. It is
 * not inferred from iPhone intensity, Android vmax or the test's gain64.
 * Requires actual firmware callback and owner admission. Retains state on error. */
int elden_aw_initialize(EldenAwChip *,const uint8_t *,size_t,uint8_t max_gain);
int elden_aw_ram_start(EldenAwChip *,uint8_t wave,uint8_t gain,uint32_t duration_us);
int elden_aw_poll(EldenAwChip *);
int elden_aw_stop(EldenAwChip *);
#ifdef __cplusplus
}
#endif
#endif
