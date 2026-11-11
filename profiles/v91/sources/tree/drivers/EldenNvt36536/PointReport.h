/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_NVT36536_POINT_REPORT_H
#define ELDEN_NVT36536_POINT_REPORT_H
#include <stddef.h>
#include <stdint.h>
#define ELDEN_NVT_POINT_BYTES 109
#define ELDEN_NVT_CONTACTS 10
struct elden_nvt_contact {uint8_t id,width;uint16_t raw_x,raw_y,x,y;};
struct elden_nvt_frame {unsigned count;struct elden_nvt_contact contacts[ELDEN_NVT_CONTACTS];};
enum {ELDEN_NVT_OK=0,ELDEN_NVT_LENGTH=-1,ELDEN_NVT_CHECKSUM=-2,ELDEN_NVT_RANGE=-3,ELDEN_NVT_DUPLICATE=-4};
/* Point buffer includes the SPI register byte at index zero. Output coordinates
 * are normalized to 0..32767, max_x/max_y are inclusive firmware maxima.
 * No destination change on failure; callers separately release stale contacts. */
int elden_nvt_decode(const uint8_t *,size_t,uint16_t,uint16_t,struct elden_nvt_frame *);
#endif
