// SPDX-License-Identifier: BSD-2-Clause-Patent
// TB322FC / elden board facts. Source: sm8750-lenovo-elden.dts and
// pinctrl-sm8750.c at 6f97250ead155fc3b980856323e99a4623dae73d.
// Stock packet coordinates include the maxima; HID conversion is separate.
#pragma once
#include "EldenTouchResources.h"
#define ELDEN_TOUCH_GPIO_MISO 48u
#define ELDEN_TOUCH_GPIO_MOSI 49u
#define ELDEN_TOUCH_GPIO_CLK 50u
#define ELDEN_TOUCH_GPIO_CS 51u
#define ELDEN_TOUCH_GPIO_IRQ 162u
#define ELDEN_TOUCH_GPIO_FUNC 1u
#define ELDEN_TOUCH_SPI_DRIVE_MA 6u
#define ELDEN_TOUCH_IRQ_DRIVE_MA 8u
#define ELDEN_TOUCH_SPI_MAX_HZ 10000000u
#define ELDEN_TOUCH_RAW_MAX_X 19040u
#define ELDEN_TOUCH_RAW_MAX_Y 30400u
#define ELDEN_TOUCH_FIRMWARE "novatek_ts_csot_3k_fw.bin"
/* Exact live v25 global SMMU snapshot (2026-11-09). This is a development
 * admission baseline, not a claim that an unused slot is automatically ours.
 * Full session-scoped owner, idle and bank0 checks are mandatory before use. */
#define ELDEN_TOUCH_DMA_SLOT 7u
#define ELDEN_TOUCH_DMA_BANK 0u
#define ELDEN_TOUCH_DMA_CB0_OFFSET 0x80000u
#define ELDEN_TOUCH_DMA_CR0 0x00230406u
#define ELDEN_TOUCH_DMA_ID0 0x4c017e84u
#define ELDEN_TOUCH_DMA_ID1 0x60000053u
#define ELDEN_TOUCH_DMA_ID2 0x00005111u
#define ELDEN_TOUCH_DMA_SLOTS 132u
#define ELDEN_TOUCH_DMA_BANKS 83u
#define ELDEN_TOUCH_DMA_UNUSED_S2CR 0x0300006eu
#define ELDEN_TOUCH_DMA_ACTIVE_SMR {0x80000060u,0x80000540u,0x80020800u,0x80000040u,0x80000480u,0x80000540u,0x80000480u}
