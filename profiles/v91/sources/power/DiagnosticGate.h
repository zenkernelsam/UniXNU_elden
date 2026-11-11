/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "PdBootstrap.h"
#include "UsbIcl.h"
/* Only controls eligibility for GET diagnostics. It never retries a pulse,
 * alters a charger vote, or loosens the control state's own admission. */
static inline bool elden_diagnostic_terminal(const EldenPdBootstrap *s,
        const EldenUsbIcl *icl, unsigned pulse, uint32_t raw_epoch,
        uint32_t observed, bool fresh, bool allow_failed) {
    if(s->phase==EldenPdBootDone&&s->error==EldenPdBootOk&&s->restored)return true;
    if(!allow_failed||s->phase!=EldenPdBootFailed||s->epoch!=raw_epoch||!fresh)return false;
    if(!icl->up||!icl->owns_limit||icl->pending||icl->quarantined||icl->suspend_requested||
       !icl->applied||icl->applied>500000||icl->applied!=observed)return false;
    if(pulse!=EldenPulseReady&&pulse!=EldenPulseRestored&&pulse!=EldenPulseRefused)return false;
    /* Unconfirmed/unfinished input restoration remains a recovery concern. */
    return s->restored||(!s->saw_zero&&!s->restore_cycle);
}
