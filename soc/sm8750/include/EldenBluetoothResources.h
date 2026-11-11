/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Source: fixed same-board Linux6f97250e, gcc-sm8750 / sm8750.dtsi. */
#pragma once
#define ELDEN_BT_GCC_BASE 0x00100000ULL
#define ELDEN_BT_GCC_BYTES 0x60000u
#define ELDEN_BT_SE_BASE 0x00898000ULL
#define ELDEN_BT_SE_BYTES 0x4000u
#define ELDEN_BT_QUP_BASE 0x008c0000ULL
#define ELDEN_BT_QUP_BYTES 0x2000u
#define ELDEN_BT_GCC_VOTE 0x52010u
#define ELDEN_BT_GCC_VOTE_MASK ((1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<10))
#define ELDEN_BT_HALT_M_AHB 0x23298u
#define ELDEN_BT_HALT_S_AHB 0x2329cu
#define ELDEN_BT_HALT_CORE 0x232a0u
#define ELDEN_BT_HALT_CORE_2X 0x232b4u
#define ELDEN_BT_HALT_SE6 0x1e76cu
#define ELDEN_BT_RCG 0x1e77cu
#define ELDEN_BT_TLMM_BASE 0x0f100000ULL
#define ELDEN_BT_GPIO_STRIDE 0x1000u
#define ELDEN_BT_SE_SPI 461u
#define ELDEN_BT_DMA_SID 0x423u
