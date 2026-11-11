/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ELDEN_NVT_FW_CHUNK 15360u
#define ELDEN_NVT_FW_PARTITIONS 32u
#define ELDEN_NVT_EVENT 0x130900u
typedef struct {uint32_t file,ram,size,crc;} EldenNvtPartition;
typedef struct {
    const uint8_t *data;size_t bytes;unsigned count;bool cascade;
    EldenNvtPartition part[ELDEN_NVT_FW_PARTITIONS];
} EldenNvtImage;
typedef struct {
    void *context;
    int (*transfer)(void *,const uint8_t *,uint8_t *,size_t);
    void (*delay_us)(void *,unsigned);
    uint8_t *tx,*rx;size_t tx_bytes,rx_bytes;
    bool poisoned,identified,ready,cascade;
    uint8_t trim[6],firmware_version,reset_state;
    uint16_t pid;
    unsigned stage,polls;int error;
} EldenNvtDevice;
enum {EldenNvtSuccess=0,EldenNvtArgument=-22,EldenNvtFirmware=-8,
      EldenNvtChip=-19,EldenNvtBus=-5,EldenNvtTimeout=-110,EldenNvtUnsupported=-95};

/* Full preflight before any reset/write; lengths in this firmware are inclusive.
 * Image bytes remain unchanged. No flash/provision command exists here.
 */
int elden_nvt_image(EldenNvtImage *,const uint8_t *,size_t);
int elden_nvt_read(EldenNvtDevice *,uint32_t,uint8_t *,size_t);
int elden_nvt_identify(EldenNvtDevice *,bool reset);
int elden_nvt_firmware_info(EldenNvtDevice *);
int elden_nvt_upload(EldenNvtDevice *,const EldenNvtImage *);
int elden_nvt_points(EldenNvtDevice *,uint8_t out[109]);
#ifdef __cplusplus
}
#endif
