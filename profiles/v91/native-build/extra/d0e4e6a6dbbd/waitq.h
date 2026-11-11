/*
 * Copyright (c) 2014-2021 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#ifndef _WAITQ_H_
#define _WAITQ_H_

#include <mach/mach_types.h>
#include <mach/sync_policy.h>
#include <mach/kern_return.h>           /* for kern_return_t */

#include <kern/kern_types.h>            /* for wait_queue_t */
#include <kern/queue.h>
#include <kern/assert.h>

#include <sys/cdefs.h>


__BEGIN_DECLS __ASSUME_PTR_ABI_SINGLE_BEGIN

#pragma GCC visibility push(hidden)

/*!
 * @enum waitq_wakeup_flags_t
 *
 * @const WAITQ_WAKEUP_DEFAULT
 * Use the default behavior for wakeup.
 *
 * @const WAITQ_UPDATE_INHERITOR
 * If the wait queue is a turnstile,
 * set its inheritor to the woken up thread,
 * or clear the inheritor if the last thread is woken up.
 *
 #if MACH_KERNEL_PRIVATE
 * @const WAITQ_PROMOTE_PRIORITY (Mach IPC only)
 * Promote the woken up thread(s) with a MINPRI_WAITQ floor,
 * until it calls waitq_clear_promotion_locked().
 *
 * @const WAITQ_UNLOCK (waitq_wakeup64_*_locked only)
 * Unlock the wait queue before any thread_go() is called for woken up threads.
 *
 * @const WAITQ_ENABLE_INTERRUPTS (waitq_wakeup64_*_locked only)
 * Also enable interrupts when unlocking the wait queue.
 *
 * @const WAITQ_KEEP_LOCKED (waitq_wakeup64_*_locked only)
 * Keep the wait queue locked for this call.
 *
 * @const WAITQ_HANDOFF (waitq_wakeup64_one, waitq_wakeup64_identify*)
 * Attempt a handoff to the woken up thread.
 #endif
 */
__options_decl(waitq_wakeup_flags_t, uint32_t, {
	WAITQ_WAKEUP_DEFAULT    = 0x0000,
	WAITQ_UPDATE_INHERITOR  = 0x0001,
});

/* Opaque sizes and alignment used for struct verification */
#if __arm__ || __arm64__
	#define WQ_OPAQUE_ALIGN   __BIGGEST_ALIGNMENT__
	#if __arm__
		#define WQ_OPAQUE_SIZE   32
	#else
		#define WQ_OPAQUE_SIZE   40
	#endif
#elif __x86_64__
	#define WQ_OPAQUE_ALIGN   8
	#define WQ_OPAQUE_SIZE   48
#else
	#error Unknown size requirement
#endif

#ifdef __cplusplus
#define __waitq_transparent_union
#else
#define __waitq_transparent_union __attribute__((__transparent_union__))
#endif

/**
 * @typedef waitq_t
 *
 * @brief
 * This is an abstract typedef used to denote waitq APIs that can be called
 * on any kind of wait queue (or wait queue set).
 */
typedef union {
	struct waitq      *wq_q;
	struct waitq_set  *wqs_set;
	struct select_set *wqs_sel;
} __waitq_transparent_union waitq_t;


/*
 * The opaque waitq structure is here mostly for AIO and selinfo,
 * but could potentially be used by other BSD subsystems.
 */
struct waitq {
	char opaque[WQ_OPAQUE_SIZE];
} __attribute__((aligned(WQ_OPAQUE_ALIGN)));


#pragma GCC visibility pop

__ASSUME_PTR_ABI_SINGLE_END __END_DECLS

#endif  /* _WAITQ_H_ */
