// SPDX-License-Identifier: BSD-2-Clause-Patent
// SM8750 GENI wrapper 1 / SE4. Register facts from h0cheung Linux
// 6f97250ead155fc3b980856323e99a4623dae73d, gcc-sm8750.c / sm8750.dtsi.
// This is the shared-RCG implementation, not sheng's SM8550 DFS layout.
#pragma once
#include <stdint.h>
#define ELDEN_TOUCH_GCC_BASE 0x00100000ULL
#define ELDEN_TOUCH_GCC_VOTE 0x52008u
#define ELDEN_TOUCH_GCC_HALT_CORE_2X 0x2315cu
#define ELDEN_TOUCH_GCC_HALT_CORE 0x23148u
#define ELDEN_TOUCH_GCC_HALT_M_AHB 0x23140u
#define ELDEN_TOUCH_GCC_HALT_S_AHB 0x23144u
#define ELDEN_TOUCH_GCC_HALT_S4 0x183ccu
#define ELDEN_TOUCH_GCC_S4_RCG 0x183dcu
#define ELDEN_TOUCH_GCC_VOTE_MASK ((1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<26))
#define ELDEN_TOUCH_GCC_CLK_OFF (1u<<31)
#define ELDEN_TOUCH_QUP_BASE 0x00ac0000ULL
#define ELDEN_TOUCH_QUP_SIZE 0x2000u
#define ELDEN_TOUCH_SE_BASE 0x00a90000ULL
#define ELDEN_TOUCH_SE_SIZE 0x4000u
#define ELDEN_TOUCH_TLMM_BASE 0x0f100000ULL
#define ELDEN_TOUCH_GPIO_STRIDE 0x1000u
/* Armada TB322FC sm8750.dtsi: SE DMA uses wrapper SIDa3; GPI DMA SIDb6.
 * These are distinct clients. Neither inherits the UFS SID60 qualification. */
#define ELDEN_TOUCH_SMMU_BASE 0x15000000ULL
#define ELDEN_TOUCH_SMMU_GLOBAL_BYTES 0x2000u
#define ELDEN_TOUCH_SE_DMA_SID 0x00a3u
#define ELDEN_TOUCH_GPI_DMA_SID 0x00b6u
