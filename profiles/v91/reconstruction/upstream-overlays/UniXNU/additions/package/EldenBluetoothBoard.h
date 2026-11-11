/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "EldenBluetoothResources.h"
#define ELDEN_BT_PIN_CTS 24u
#define ELDEN_BT_PIN_RTS 25u
#define ELDEN_BT_PIN_TX 26u
#define ELDEN_BT_PIN_RX 27u
#define ELDEN_BT_PIN_WLAN_EN 16u
#define ELDEN_BT_PIN_SW_CTRL 18u
#define ELDEN_BT_GPIO_FUNC 1u
#define ELDEN_BT_GPIO_DRIVE_MA 2u
#define ELDEN_BT_ENABLE_PPID 0x58au
#define ELDEN_BT_INITIAL_BAUD 115200u
#define ELDEN_BT_DOWNLOAD_BAUD 8000000u
/* The stock normal BT/WLAN power path leaves RF_CLK1 off; its FMD/UPDATE_SOC_VER
 * path is separate. Do not copy Pagani's unconditional RF clock vote here. */
#define ELDEN_BT_NORMAL_RF_CLK1_REQUIRED 0u
/* PMU supplies: shared S5F, L3F, L2F, S4D, S7I, S3G. Preserve already-live
 * shared rails and apply QMP/PDC through the common WCN owner before BT_EN.
 */
#define ELDEN_BT_PDC_UP_MV 876u
#define ELDEN_BT_ENABLE_LOW_MS 50u
#define ELDEN_BT_ENABLE_HIGH_MS 50u
