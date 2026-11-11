/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* TB322FC stock DT: qcom,cpucp-v2, TX channel0 / RX channel2.
 * Stock Android reports scmi policies0(CPU0..5) and6(CPU6..7).
 */
#pragma once
#define ELDEN_CPUCP_TX_BASE 0x17830000ULL
#define ELDEN_CPUCP_TX_BYTES 0x300u
#define ELDEN_CPUCP_DOORBELL 0x104u
#define ELDEN_CPUCP_SCMI_BASE 0x17b4e000ULL
#define ELDEN_CPUCP_SCMI_BYTES 0x200u
