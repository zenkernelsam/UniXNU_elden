/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_QRTR_TRANSPORT_H
#define ELDEN_MOTION_QRTR_TRANSPORT_H
#include "NativeSsc.h"
#include "QrtrPorts.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicitly called by original ADSP/sensor owner AFTER installing real
 * QrtrPortOps, before native_ssc_start. No new thread/router/automatic start. */
int elden_motion_qrtr_transport(EldenMotionTransport *);
#ifdef __cplusplus
}
#endif
#endif
