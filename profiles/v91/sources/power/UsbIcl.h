/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "ChargePoll.h"
#include "UcsiQuery.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool up,pending,quarantined,owns_limit,suspend_requested;
    bool sdp_only,deferred;
    uint32_t epoch,requested,applied,requests,acks,timeouts,ret_code,request_cycle;
} EldenUsbIcl;
void elden_usb_icl_init(EldenUsbIcl *s);
/* Opt-in typed commands: unknown/mixed/PD input belongs to the firmware.
 * Keep the lease bookkeeping; do not pretend an omitted write was applied. */
void elden_usb_icl_sdp_only(EldenUsbIcl *s,bool enabled);
void elden_usb_icl_link(EldenUsbIcl *s,bool up);
/* Single executor only. The PD experiment may pause its own qualified budget;
 * loss of prerequisites immediately returns to the normal safe budget. */
void elden_usb_icl_suspend(EldenUsbIcl *s,bool suspend);
/* Only SDP and a live configured USB2 data connection may request 500mA.
 * Legacy mode restores 100mA (2mA for USB suspend) on loss of budget.
 * SDP-only mode sends those reductions only with fresh matching SDP identity;
 * unknown/mixed/PD identity defers commands without claiming a restoration.
 * All policy inputs must belong to the current coherent telemetry epoch.
 */
int elden_usb_icl_next(EldenUsbIcl *s,const EldenChargeRawSnapshot *raw,
    const EldenUcsiSnapshot *ucsi,bool fresh,bool notified,uint32_t usb_budget_ua,uint8_t out[24]);
void elden_usb_icl_unsent(EldenUsbIcl *s);
void elden_usb_icl_timeout(EldenUsbIcl *s);
int elden_usb_icl_receive(EldenUsbIcl *s,const uint8_t *data,size_t bytes);
#ifdef __cplusplus
}
#endif
