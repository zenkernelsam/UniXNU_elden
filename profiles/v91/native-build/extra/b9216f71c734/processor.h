/*
 * Copyright (c) 2000-2019 Apple Inc. All rights reserved.
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
/*
 * @OSF_COPYRIGHT@
 */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 */

/*
 *	processor.h:	Processor and processor-related definitions.
 */

#ifndef _KERN_PROCESSOR_H_
#define _KERN_PROCESSOR_H_

#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <kern/kern_types.h>

#include <sys/cdefs.h>


__BEGIN_DECLS __ASSUME_PTR_ABI_SINGLE_BEGIN


extern unsigned int     processor_count;
extern processor_t      cpu_to_processor(int cpu);

extern kern_return_t    enable_smt_processors(bool enable);

/*
 * Update the scheduler with the set of cores that should be used to dispatch new threads.
 * Non-recommended cores can still be used to field interrupts or run bound threads.
 * This should be called with interrupts enabled and no scheduler locks held.
 */
#define ALL_CORES_RECOMMENDED   (~(uint64_t)0)
#define ALL_CORES_POWERED       (~(uint64_t)0)

extern void sched_perfcontrol_update_recommended_cores(uint32_t recommended_cores);
extern void sched_perfcontrol_update_recommended_cores_reason(uint64_t recommended_cores, processor_reason_t reason, uint32_t flags);
extern void sched_perfcontrol_update_powered_cores(uint64_t powered_cores, processor_reason_t reason, uint32_t flags);
extern void sched_override_available_cores_for_sleep(void);
extern void sched_restore_available_cores_after_sleep(void);
extern bool sched_is_in_sleep(void);
extern void sched_mark_processor_online_locked(processor_t processor, processor_reason_t reason);
extern kern_return_t sched_mark_processor_offline(processor_t processor, processor_reason_t reason);
extern bool processor_should_kprintf(processor_t processor, bool starting);
extern void suspend_cluster_powerdown(void);
extern void resume_cluster_powerdown(void);
extern kern_return_t suspend_cluster_powerdown_from_user(void);
extern kern_return_t resume_cluster_powerdown_from_user(void);
extern int get_cluster_powerdown_user_suspended(void);


__ASSUME_PTR_ABI_SINGLE_END __END_DECLS

#endif  /* _KERN_PROCESSOR_H_ */
