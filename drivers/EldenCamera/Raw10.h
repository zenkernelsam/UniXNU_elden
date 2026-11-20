/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_RAW10_H
#define ELDEN_CAMERA_RAW10_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRawRggb=0,EldenRawGrbg=1,EldenRawGbrg=2,EldenRawBggr=3};
typedef struct {
    uint32_t width,height,stride,bayer,rotation,mirror,output_width,output_height;
    /* Explicit software preview/calibration only; never sensor gain writes.
     * Zero black + unity white balance is a visibly uncalibrated preview.
     * No assumed T970 pedestal, mechanical/electrical motor gain or ISP. */
    uint16_t black,white;uint32_t gain_q16[3];
} EldenRaw10Config;
/* Packed CSI RAW10 -> centre-cropped, nearest Bayer-cell BGRA8/sRGB preview.
 * Uses all ten bits; input remains borrowed and must not be requeued during
 * conversion. Rotation/mirror and calibration are explicit, not guessed.
 * Every span/stride/buffer and alias check precedes writes. Padding retained.
 * No frame generator, hardware capture, sensor write or GPU dependency. */
int elden_raw10_bgra(const EldenRaw10Config *,const uint8_t *raw,size_t raw_bytes,
    uint8_t *bgra,size_t bgra_bytes,size_t output_stride);
#ifdef __cplusplus
}
#endif
#endif
