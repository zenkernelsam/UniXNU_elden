/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
/* TB322FC: Armada's I2C10 is wrapper 2 / SE2, Android adapter 4. The
 * original driver trace identifies AW99706 at 0x76, not the alternate 0x11.
 * GCC/pin facts: pinned gcc-sm8750.c, pinctrl-sm8750.c and SM8750 DTSI. */
#define ELDEN_BL_GCC_BASE 0x00100000ULL
#define ELDEN_BL_GCC_BYTES 0x60000u
#define ELDEN_BL_GCC_VOTE 0x52010u
#define ELDEN_BL_GCC_COMMON 0x0fu
#define ELDEN_BL_GCC_SE_VOTE (1u << 6)
/* SE7 is bit17, not bit11; wrapper2 IBI clients use bits27/28. */
#define ELDEN_BL_GCC_SIBLINGS (((0x7fu<<4)|(1u<<17)|(1u<<27)|(1u<<28)) & ~ELDEN_BL_GCC_SE_VOTE)
#define ELDEN_BL_GCC_RCG 0x1e28cu
#define ELDEN_BL_SE_BASE 0x00888000ULL
#define ELDEN_BL_SE_BYTES 0x4000u
#define ELDEN_BL_TLMM_BASE 0x0f100000ULL
#define ELDEN_BL_GPIO_STRIDE 0x1000u
#define ELDEN_BL_SDA 8u
#define ELDEN_BL_SCL 9u
#define ELDEN_BL_HWEN 88u
#define ELDEN_BL_PIN_FUNCTION 1u
#define ELDEN_BL_ADDRESS 0x76u
