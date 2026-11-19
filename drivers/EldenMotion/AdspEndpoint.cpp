/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdspEndpoint.h"
#include "AdspChannel.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
extern "C" boolean_t ml_at_interrupt_context(void);
#include "AdspEndpointMethods.inc"
