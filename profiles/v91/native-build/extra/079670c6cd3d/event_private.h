/*
 * Copyright (c) 2003-2021 Apple Inc. All rights reserved.
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
/*-
 * Copyright (c) 1999,2000,2001 Jonathan Lemon <jlemon@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/sys/sys/event.h,v 1.5.2.5 2001/12/14 19:21:22 jlemon Exp $
 */

#ifndef _SYS_EVENT_PRIVATE_H_
#define _SYS_EVENT_PRIVATE_H_

#include <machine/types.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/event.h>
#include <sys/queue.h>

/*
 * Filter types
 */
/* Additional filter types in event.h */
#define EVFILT_UNUSED_11        (-11)   /* (-11) unused */
#define EVFILT_SOCK             (-13)   /* Socket events */
#define EVFILT_MEMORYSTATUS     (-14)   /* Memorystatus events */
#define EVFILT_NW_CHANNEL       (-16)   /* Skywalk channel events */
#define EVFILT_WORKLOOP         (-17)   /* Workloop events */
#define EVFILT_EXCLAVES_NOTIFICATION (-18) /* Exclave notification */
/* If additional filter types are added, make sure to update
 * EVFILT_SYSCOUNT in event.h!!!
 */


#pragma pack(4)

struct user64_kevent {
	uint64_t        ident;  /* identifier for this event */
	int16_t         filter; /* filter for event */
	uint16_t        flags;  /* general flags */
	uint32_t        fflags; /* filter-specific flags */
	int64_t         data;   /* filter-specific data */
	user_addr_t     udata;  /* opaque user data identifier */
};

struct user32_kevent {
	uint32_t        ident;  /* identifier for this event */
	int16_t         filter; /* filter for event */
	uint16_t        flags;  /* general flags */
	uint32_t        fflags; /* filter-specific flags */
	int32_t         data;   /* filter-specific data */
	user32_addr_t   udata;  /* opaque user data identifier */
};

#pragma pack()


struct kevent_qos_s {
	uint64_t        ident;          /* identifier for this event */
	int16_t         filter;         /* filter for event */
	uint16_t        flags;          /* general flags */
	int32_t         qos;            /* quality of service */
	uint64_t        udata;          /* opaque user data identifier */
	uint32_t        fflags;         /* filter-specific flags */
	uint32_t        xflags;         /* extra filter-specific flags */
	int64_t         data;           /* filter-specific data */
	uint64_t        ext[4];         /* filter-specific extensions */
};

/*
 * Type definition for names/ids of dynamically allocated kqueues.
 */
typedef uint64_t kqueue_id_t;

/*
 * Rather than provide an EV_SET_QOS macro for kevent_qos_t structure
 * initialization, we encourage use of named field initialization support
 * instead.
 */

// was  KEVENT_FLAG_STACK_EVENTS                 0x000004
#define KEVENT_FLAG_STACK_DATA                   0x000008   /* output data allocated as stack (grows down) */
//      KEVENT_FLAG_POLL                         0x000010
#define KEVENT_FLAG_WORKQ                        0x000020   /* interact with the default workq kq */
//      KEVENT_FLAG_LEGACY32                     0x000040
//      KEVENT_FLAG_LEGACY64                     0x000080
//      KEVENT_FLAG_PROC64                       0x000100
#define KEVENT_FLAG_WORKQ_MANAGER                0x000200   /* obsolete */
#define KEVENT_FLAG_WORKLOOP                     0x000400   /* interact with the specified workloop kq */
#define KEVENT_FLAG_PARKING                      0x000800   /* workq thread is parking */
//      KEVENT_FLAG_KERNEL                       0x001000
//      KEVENT_FLAG_DYNAMIC_KQUEUE               0x002000
//      KEVENT_FLAG_NEEDS_END_PROCESSING         0x004000
#define KEVENT_FLAG_WORKLOOP_SERVICER_ATTACH     0x008000   /* obsolete */
#define KEVENT_FLAG_WORKLOOP_SERVICER_DETACH     0x010000   /* obsolete */
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_EXIST        0x020000   /* kq lookup by id must exist */
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_NOT_EXIST    0x040000   /* kq lookup by id must not exist */
#define KEVENT_FLAG_WORKLOOP_NO_WQ_THREAD        0x080000   /* obsolete */


#define EV_SET_QOS 0

/*
 * data/hint fflags for EVFILT_WORKLOOP, shared with userspace
 *
 * The ident for thread requests should be the dynamic ID of the workloop
 * The ident for each sync waiter must be unique to that waiter [for this workloop]
 *
 *
 * Commands:
 *
 * @const NOTE_WL_THREAD_REQUEST [in/out]
 * The kevent represents asynchronous userspace work and its associated QoS.
 * There can only be a single knote with this flag set per workloop.
 *
 * @const NOTE_WL_SYNC_WAIT [in/out]
 * This bit is set when the caller is waiting to become the owner of a workloop.
 * If the NOTE_WL_SYNC_WAKE bit is already set then the caller is not blocked,
 * else it blocks until it is set.
 *
 * The QoS field of the knote is used to push on other owners or servicers.
 *
 * @const NOTE_WL_SYNC_WAKE [in/out]
 * Marks the waiter knote as being eligible to become an owner
 * This bit can only be set once, trying it again will fail with EALREADY.
 *
 * @const NOTE_WL_SYNC_IPC [in/out]
 * The knote is a sync IPC redirected turnstile push.
 *
 * Flags/Modifiers:
 *
 * @const NOTE_WL_UPDATE_QOS [in] (only NOTE_WL_THREAD_REQUEST)
 * For successful updates (EV_ADD only), learn the new userspace async QoS from
 * the kevent qos field.
 *
 * @const NOTE_WL_END_OWNERSHIP [in]
 * If the update is successful (including deletions) or returns ESTALE, and
 * the caller thread or the "suspended" thread is currently owning the workloop,
 * then ownership is forgotten.
 *
 * @const NOTE_WL_DISCOVER_OWNER [in]
 * If the update is successful (including deletions), learn the owner identity
 * from the loaded value during debounce. This requires an address to have been
 * filled in the EV_EXTIDX_WL_ADDR ext field, but doesn't require a mask to have
 * been set in the EV_EXTIDX_WL_MASK.
 *
 * @const NOTE_WL_IGNORE_ESTALE [in]
 * If the operation would fail with ESTALE, mask the error and pretend the
 * update was successful. However the operation itself didn't happen, meaning
 * that:
 * - attaching a new knote will not happen
 * - dropping an existing knote will not happen
 * - NOTE_WL_UPDATE_QOS or NOTE_WL_DISCOVER_OWNER will have no effect
 *
 * This modifier doesn't affect NOTE_WL_END_OWNERSHIP.
 */
#define NOTE_WL_THREAD_REQUEST   0x00000001
#define NOTE_WL_SYNC_WAIT        0x00000004
#define NOTE_WL_SYNC_WAKE        0x00000008
#define NOTE_WL_SYNC_IPC         0x80000000
#define NOTE_WL_COMMANDS_MASK    0x8000000f /* Mask of all the [in] commands above */

#define NOTE_WL_UPDATE_QOS       0x00000010
#define NOTE_WL_END_OWNERSHIP    0x00000020
#define NOTE_WL_DISCOVER_OWNER   0x00000080
#define NOTE_WL_IGNORE_ESTALE    0x00000100
#define NOTE_WL_UPDATES_MASK     0x000001f0 /* Mask of all the [in] updates above */

#define NOTE_WL_UPDATE_OWNER     0 /* ... compatibility define ... */

/*
 * EVFILT_WORKLOOP ext[] array indexes/meanings.
 */
#define EV_EXTIDX_WL_LANE        0         /* lane identifier  [in: sync waiter]
	                                    *                  [out: thread request]     */
#define EV_EXTIDX_WL_ADDR        1         /* debounce address [in: NULL==no debounce]   */
#define EV_EXTIDX_WL_MASK        2         /* debounce mask    [in]                      */
#define EV_EXTIDX_WL_VALUE       3         /* debounce value   [in: not current->ESTALE]
	                                    *                  [out: new/debounce value] */

/*
 * If NOTE_EXIT_MEMORY is present, these bits indicate specific jetsam condition.
 */
#define NOTE_EXIT_MEMORY_DETAIL_MASK    0xfe000000
#define NOTE_EXIT_MEMORY_VMPAGESHORTAGE 0x80000000      /* jetsam condition: lowest jetsam priority proc killed due to vm page shortage */
#define NOTE_EXIT_MEMORY_VMTHRASHING    0x40000000      /* jetsam condition: lowest jetsam priority proc killed due to vm thrashing */
#define NOTE_EXIT_MEMORY_HIWAT          0x20000000      /* jetsam condition: process reached its high water mark */
#define NOTE_EXIT_MEMORY_PID            0x10000000      /* jetsam condition: special pid kill requested */
#define NOTE_EXIT_MEMORY_IDLE           0x08000000      /* jetsam condition: idle process cleaned up */
#define NOTE_EXIT_MEMORY_VNODE          0X04000000      /* jetsam condition: virtual node kill */
#define NOTE_EXIT_MEMORY_FCTHRASHING    0x02000000      /* jetsam condition: lowest jetsam priority proc killed due to filecache thrashing */

/*
 * data/hint fflags for EVFILT_MEMORYSTATUS, shared with userspace.
 */
#define NOTE_MEMORYSTATUS_PRESSURE_NORMAL       0x00000001      /* system memory pressure has returned to normal */
#define NOTE_MEMORYSTATUS_PRESSURE_WARN         0x00000002      /* system memory pressure has changed to the warning state */
#define NOTE_MEMORYSTATUS_PRESSURE_CRITICAL     0x00000004      /* system memory pressure has changed to the critical state */
#define NOTE_MEMORYSTATUS_LOW_SWAP              0x00000008      /* system is in a low-swap state */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_WARN       0x00000010      /* process memory limit has hit a warning state */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL   0x00000020      /* process memory limit has hit a critical state - soft limit */
#define NOTE_MEMORYSTATUS_MSL_STATUS   0xf0000000      /* bits used to request change to process MSL status */

/*
 * data/hint fflags for EVFILT_MEMORYSTATUS, but not shared with userspace.
 */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_WARN_ACTIVE        0x00000040      /* Used to restrict sending a warn event only once, per active limit, soft limits only */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_WARN_INACTIVE      0x00000080      /* Used to restrict sending a warn event only once, per inactive limit, soft limit only */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL_ACTIVE    0x00000100      /* Used to restrict sending a critical event only once per active limit, soft limit only */
#define NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL_INACTIVE  0x00000200      /* Used to restrict sending a critical event only once per inactive limit, soft limit only */
#define NOTE_MEMORYSTATUS_JETSAM_FG_BAND                0x00000400      /* jetsam is approaching foreground band */

/*
 * Use this mask to protect the kernel private flags.
 */
#define EVFILT_MEMORYSTATUS_ALL_MASK \
	(NOTE_MEMORYSTATUS_PRESSURE_NORMAL | NOTE_MEMORYSTATUS_PRESSURE_WARN | NOTE_MEMORYSTATUS_PRESSURE_CRITICAL | NOTE_MEMORYSTATUS_LOW_SWAP | \
	 NOTE_MEMORYSTATUS_PROC_LIMIT_WARN | NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL | NOTE_MEMORYSTATUS_MSL_STATUS)


typedef enum vm_pressure_level {
	kVMPressureNormal   = 0,
	kVMPressureWarning  = 1,
	kVMPressureUrgent   = 2,
	kVMPressureCritical = 3,
	kVMPressureJetsam   = 4,  /* jetsam approaching FG bands */
} vm_pressure_level_t;

/*
 * data/hint fflags for EVFILT_SOCK, shared with userspace.
 *
 */
#define NOTE_CONNRESET          0x00000001 /* Received RST */
#define NOTE_READCLOSED         0x00000002 /* Read side is shutdown */
#define NOTE_WRITECLOSED        0x00000004 /* Write side is shutdown */
#define NOTE_TIMEOUT            0x00000008 /* timeout: rexmt, keep-alive or persist */
#define NOTE_NOSRCADDR          0x00000010 /* source address not available */
#define NOTE_IFDENIED           0x00000020 /* interface denied connection */
#define NOTE_SUSPEND            0x00000040 /* output queue suspended */
#define NOTE_RESUME             0x00000080 /* output queue resumed */
#define NOTE_KEEPALIVE          0x00000100 /* TCP Keepalive received */
#define NOTE_ADAPTIVE_WTIMO     0x00000200 /* TCP adaptive write timeout */
#define NOTE_ADAPTIVE_RTIMO     0x00000400 /* TCP adaptive read timeout */
#define NOTE_CONNECTED          0x00000800 /* socket is connected */
#define NOTE_DISCONNECTED       0x00001000 /* socket is disconnected */
#define NOTE_CONNINFO_UPDATED   0x00002000 /* connection info was updated */
#define NOTE_NOTIFY_ACK         0x00004000 /* notify acknowledgement */
#define NOTE_WAKE_PKT           0x00008000 /* received wake packet */

#define EVFILT_SOCK_LEVEL_TRIGGER_MASK \
	        (NOTE_READCLOSED | NOTE_WRITECLOSED | NOTE_SUSPEND | NOTE_RESUME | \
	         NOTE_CONNECTED | NOTE_DISCONNECTED)

#define EVFILT_SOCK_ALL_MASK \
	        (NOTE_CONNRESET | NOTE_READCLOSED | NOTE_WRITECLOSED | NOTE_TIMEOUT | \
	        NOTE_NOSRCADDR | NOTE_IFDENIED | NOTE_SUSPEND | NOTE_RESUME | \
	        NOTE_KEEPALIVE | NOTE_ADAPTIVE_WTIMO | NOTE_ADAPTIVE_RTIMO | \
	        NOTE_CONNECTED | NOTE_DISCONNECTED | NOTE_CONNINFO_UPDATED | \
	        NOTE_NOTIFY_ACK | NOTE_WAKE_PKT)

/*
 * data/hint fflags for EVFILT_NW_CHANNEL, shared with userspace.
 */
#define NOTE_FLOW_ADV_UPDATE    0x00000001 /* flow advisory update */
#define NOTE_CHANNEL_EVENT      0x00000002 /* generic channel event */
#define NOTE_IF_ADV_UPD         0x00000004 /* Interface advisory update */

#define EVFILT_NW_CHANNEL_ALL_MASK    \
    (NOTE_FLOW_ADV_UPDATE | NOTE_CHANNEL_EVENT | NOTE_IF_ADV_UPD)



struct proc;
struct knote;
SLIST_HEAD(klist, knote);



/* Flags for pending events notified by kernel via return-to-kernel ast */
#define R2K_WORKLOOP_PENDING_EVENTS             0x1
#define R2K_WORKQ_PENDING_EVENTS                0x2

/* Flags for notifying what to do when there is a workqueue quantum expiry */
#define PTHREAD_WQ_QUANTUM_EXPIRY_NARROW 0x1
#define PTHREAD_WQ_QUANTUM_EXPIRY_SHUFFLE 0x2

#endif /* !_SYS_EVENT_PRIVATE_H_ */
