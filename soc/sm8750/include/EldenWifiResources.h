/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
/* Fixed Y700 source/stock GPT; no controller activation is granted here.
 * Full PCIe/SID/MSI board evidence: work lab/records/wifi-elden-20261108/inputs.json.
 */
#define ELDEN_WIFI_MODEM_LUN 4u
#define ELDEN_WIFI_MODEM_FIRST 4870ull
#define ELDEN_WIFI_MODEM_BLOCKS 89600ull
#define ELDEN_WIFI_PCI_VENDOR 0x17cbu
#define ELDEN_WIFI_PCI_DEVICE 0x110eu
#define ELDEN_WIFI_ENDPOINT_RID 0x100u
#define ELDEN_WIFI_ENDPOINT_SID 0x1401u
#define ELDEN_WIFI_SMMU_BASE 0x15000000ull
#define ELDEN_WIFI_MSI_IOVA 0x18000000ull /* IOVA, not a qualified physical target */
#define ELDEN_WIFI_WLAN_ENABLE_GPIO 16u
#define ELDEN_WIFI_PERST_GPIO 102u
#define ELDEN_WIFI_WAKE_GPIO 104u
