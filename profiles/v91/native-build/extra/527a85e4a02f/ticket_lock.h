/*
 * Copyright (c) 2021 Apple Inc. All rights reserved.
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

#ifndef _KERN_TICKET_LOCK_H_
#define _KERN_TICKET_LOCK_H_

#ifndef __ASSEMBLER__
#include <kern/lock_types.h>
#include <kern/lock_group.h>
#endif /* __ASSEMBLER__ */

#ifndef __ASSEMBLER__

__BEGIN_DECLS
#pragma GCC visibility push(hidden)


typedef struct {
	uint32_t                opaque0;
	uint32_t                opaque1;
	uint32_t                opaque2;
	uint32_t                opaque3;
} lck_ticket_t;


extern __exported void lck_ticket_init(
	lck_ticket_t            *tlock,
	lck_grp_t               *grp);

extern __exported void lck_ticket_destroy(
	lck_ticket_t            *tlock,
	lck_grp_t               *grp);

extern __exported void lck_ticket_lock(
	lck_ticket_t            *tlock,
	lck_grp_t               *grp);

extern __exported void lck_ticket_unlock(
	lck_ticket_t            *tlock);

extern __exported void lck_ticket_assert_owned(
	const lck_ticket_t            *tlock);

extern __exported void lck_ticket_assert_not_owned(
	const lck_ticket_t            *tlock);

#if MACH_ASSERT
#define LCK_TICKET_ASSERT_OWNED(tlock)     lck_ticket_assert_owned(tlock)
#define LCK_TICKET_ASSERT_NOT_OWNED(tlock) lck_ticket_assert_owned(tlock)
#else
#define LCK_TICKET_ASSERT_OWNED(tlock)     (void)(tlock)
#define LCK_TICKET_ASSERT_NOT_OWNED(tlock) (void)(tlock)
#endif

#pragma GCC visibility pop
__END_DECLS

#endif /* __ASSEMBLER__ */
#endif /* _KERN_TICKET_LOCK_H_ */
