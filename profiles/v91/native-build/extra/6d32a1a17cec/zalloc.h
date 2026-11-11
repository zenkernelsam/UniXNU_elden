/*
 * Copyright (c) 2000-2020 Apple Inc. All rights reserved.
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
 * Copyright (c) 1991,1990,1989,1988,1987 Carnegie Mellon University
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
 *	File:	zalloc.h
 *	Author:	Avadis Tevanian, Jr.
 *	Date:	 1985
 *
 */


#ifndef _KERN_ZALLOC_H_
#define _KERN_ZALLOC_H_

#include <mach/machine/vm_types.h>
#include <mach_debug/zone_info.h>
#include <kern/kern_types.h>
#include <sys/cdefs.h>
#include <os/alloc_util.h>
#include <os/atomic.h>


#define __zalloc_deprecated(msg)

/*
 * Enable this macro to force type safe zalloc/zalloc_ro/...
 */
#ifndef ZALLOC_TYPE_SAFE
#if __has_ptrcheck
#define ZALLOC_TYPE_SAFE 1
#else
#define ZALLOC_TYPE_SAFE 0
#endif
#endif /* !ZALLOC_TYPE_SAFE */

__BEGIN_DECLS __ASSUME_PTR_ABI_SINGLE_BEGIN

/*!
 * @macro __zpercpu
 *
 * @abstract
 * Annotation that helps denoting a per-cpu pointer that requires usage of
 * @c zpercpu_*() for access.
 */
#define __zpercpu __unsafe_indexable

/*!
 * @typedef zone_id_t
 *
 * @abstract
 * The type for a zone ID.
 */
typedef uint16_t zone_id_t;

/**
 * @enum zone_create_flags_t
 *
 * @abstract
 * Set of flags to pass to zone_create().
 *
 * @discussion
 * Some kernel-wide policies affect all possible created zones.
 * Explicit @c ZC_* win over such policies.
 */
__options_decl(zone_create_flags_t, uint64_t, {
	/** The default value to pass to zone_create() */
	ZC_NONE                 = 0x00000000,

	/** (obsolete) */
	ZC_SEQUESTER            = 0x00000001,
	/** (obsolete) */
	ZC_NOSEQUESTER          = 0x00000002,

	/** Enable per-CPU zone caching for this zone */
	ZC_CACHING              = 0x00000010,
	/** Disable per-CPU zone caching for this zone */
	ZC_NOCACHING            = 0x00000020,

	/** Allocate zone pages as Read-only **/
	ZC_READONLY             = 0x00800000,

	/** Mark zone as a per-cpu zone */
	ZC_PERCPU               = 0x01000000,

	/** Force the created zone to clear every allocation on free */
	ZC_ZFREE_CLEARMEM       = 0x02000000,

	/** Mark zone as non collectable by zone_gc() */
	ZC_NOGC                 = 0x04000000,

	/** Do not encrypt this zone during hibernation */
	ZC_NOENCRYPT            = 0x08000000,

	/** Type requires alignment to be preserved */
	ZC_ALIGNMENT_REQUIRED   = 0x10000000,

	/** Obsolete */
	ZC_NOGZALLOC            = 0x20000000,

	/** Don't asynchronously replenish the zone via callouts */
	ZC_NOCALLOUT            = 0x40000000,

	/** Can be zdestroy()ed, not default unlike zinit() */
	ZC_DESTRUCTIBLE         = 0x80000000,

});

/*!
 * @union zone_or_view
 *
 * @abstract
 * A type used for calls that admit both a zone or a zone view.
 *
 * @discussion
 * @c zalloc() and @c zfree() and their variants can act on both
 * zones and zone views.
 */
union zone_or_view {
	struct kalloc_type_view    *zov_kt_heap;
	struct zone_view           *zov_view;
	struct zone                *zov_zone;
#ifdef __cplusplus
	inline zone_or_view(struct zone_view *zv) : zov_view(zv) {
	}
	inline zone_or_view(struct zone *z) : zov_zone(z) {
	}
	inline zone_or_view(struct kalloc_type_view *kth) : zov_kt_heap(kth) {
	}
#endif
};
#ifdef __cplusplus
typedef union zone_or_view zone_or_view_t;
#else
typedef union zone_or_view zone_or_view_t __attribute__((transparent_union));
#endif

/*!
 * @enum zone_create_ro_id_t
 *
 * @abstract
 * Zone creation IDs for external read only zones
 *
 * @discussion
 * Kexts that desire to use the RO allocator should:
 * 1. Add a zone creation id below
 * 2. Add a corresponding ID to @c zone_reserved_id_t
 * 3. Use @c zone_create_ro with ID from #1 to create a RO zone.
 * 4. Save the zone ID returned from #3 in a SECURITY_READ_ONLY_LATE variable.
 * 5. Use the saved ID for zalloc_ro/zfree_ro, etc.
 */
__enum_decl(zone_create_ro_id_t, zone_id_t, {
	ZC_RO_ID_SANDBOX,
	ZC_RO_ID_PROFILE,
	ZC_RO_ID_PROTOBOX,
	ZC_RO_ID_SB_FILTER,
	ZC_RO_ID_AMFI_OSENTITLEMENTS,
	ZC_RO_ID__LAST = ZC_RO_ID_AMFI_OSENTITLEMENTS,
});

/*!
 * @function zone_create
 *
 * @abstract
 * Creates a zone with the specified parameters.
 *
 * @discussion
 * A Zone is a slab allocator that returns objects of a given size very quickly.
 *
 * @param name          the name for the new zone.
 * @param size          the size of the elements returned by this zone.
 * @param flags         a set of @c zone_create_flags_t flags.
 *
 * @returns             the created zone, this call never fails.
 */
extern zone_t   zone_create(
	const char             *name __unsafe_indexable,
	vm_size_t               size,
	zone_create_flags_t     flags);

/*!
 * @function zone_create_ro
 *
 * @abstract
 * Creates a read only zone with the specified parameters from kexts
 *
 * @discussion
 * See notes under @c zone_create_ro_id_t wrt creation and use of RO zones in
 * kexts. Do not use this API to create read only zones in xnu.
 *
 * @param name          the name for the new zone.
 * @param size          the size of the elements returned by this zone.
 * @param flags         a set of @c zone_create_flags_t flags.
 * @param zc_ro_id      an ID declared in @c zone_create_ro_id_t
 *
 * @returns             the zone ID of the created zone, this call never fails.
 */
extern zone_id_t   zone_create_ro(
	const char             *name __unsafe_indexable,
	vm_size_t               size,
	zone_create_flags_t     flags,
	zone_create_ro_id_t     zc_ro_id);

/*!
 * @function zdestroy
 *
 * @abstract
 * Destroys a zone previously made with zone_create.
 *
 * @discussion
 * Zones must have been made destructible for @c zdestroy() to be allowed,
 * passing @c ZC_DESTRUCTIBLE at @c zone_create() time.
 *
 * @param zone          the zone to destroy.
 */
extern void     zdestroy(
	zone_t          zone);

/*!
 * @function zone_require
 *
 * @abstract
 * Requires for a given pointer to belong to the specified zone.
 *
 * @discussion
 * The function panics if the check fails as it indicates that the kernel
 * internals have been compromised.
 *
 * @param zone          the zone the address needs to belong to.
 * @param addr          the element address to check.
 */
extern void     zone_require(
	zone_t          zone,
	void           *addr __unsafe_indexable);

/*!
 * @function zone_require_ro
 *
 * @abstract
 * Version of zone require intended for zones created with ZC_READONLY
 *
 * @discussion
 * This check is not sufficient to fully trust the element.
 *
 * Another check of its content must be performed to prove
 * that the element is "the right one", a typical technique
 * for when the RO data structure is 1:1 with a mutable one,
 * is a simple circularity check with a very strict lifetime
 * (both the mutable and read-only data structures are made
 * and destroyed as close as possible).
 *
 * @param zone_id       the zone id the address needs to belong to.
 * @param elem_size     the element size for this zone.
 * @param addr          the element address to check.
 */
extern void     zone_require_ro(
	zone_id_t       zone_id,
	vm_size_t       elem_size,
	void           *addr __unsafe_indexable);

/*!
 * @enum zalloc_flags_t
 *
 * @brief
 * Flags that can be passed to @c zalloc_internal or @c zalloc_flags.
 *
 * @discussion
 * It is encouraged that any callsite passing flags uses exactly one of:
 * @c Z_WAITOK, @c Z_NOWAIT or @c Z_NOPAGEWAIT, the default being @c Z_WAITOK
 * if nothing else was specified.
 *
 * If any @c Z_NO*WAIT flag is passed alongside @c Z_WAITOK,
 * then @c Z_WAITOK is ignored.
 *
 * @const Z_WAITOK
 * Passing this flag means that zalloc() will be allowed to sleep
 * for memory to become available for this allocation. If the zone
 * isn't exhaustible, zalloc(Z_WAITOK) never fails.
 *
 * If the zone is exhaustible, zalloc() might still fail if the zone
 * is at its maximum allowed memory usage, unless Z_NOFAIL is passed,
 * in which case zalloc() will block until an element is freed.
 *
 * @const Z_NOWAIT
 * Passing this flag means that zalloc is not allowed to ever block.
 *
 * @const Z_NOPAGEWAIT
 * Passing this flag means that zalloc is allowed to wait due to lock
 * contention, but will not wait for the VM to wait for pages when
 * under memory pressure.
 *
 * @const Z_ZERO
 * Passing this flags means that the returned memory has been zeroed out.
 *
 * @const Z_NOFAIL
 * Passing this flag means that the caller expects the allocation to always
 * succeed. This will result in a panic if this assumption isn't correct.
 *
 * This flag is incompatible with @c Z_NOWAIT or @c Z_NOPAGEWAIT.
 * For exhaustible zones, it forces the caller to wait until a zfree() happend
 * if the zone has reached its maximum of allowed elements.
 *
 * @const Z_REALLOCF
 * For the realloc family of functions,
 * free the incoming memory on failure cases.
 *
 #if XNU_KERNEL_PRIVATE
 * @const Z_SET_NOTSHARED
 * Using this flag from external allocations APIs (kalloc_type/zalloc)
 * allows the callsite to skip the shared zone for that sizeclass and
 * directly allocated from the requested zone.
 * Using this flag from internal APIs (zalloc_ext) will skip the shared
 * zone only when a given threshold is exceeded. It will also set a flag
 * to indicate that future allocations to the zone should directly go to
 * the zone instead of the shared zone.
 *
 * @const Z_SPRAYQTN
 * This flag tells the VM to allocate from the "spray quarantine" range when
 * it services the allocation. For more details on what allocations qualify
 * to use this flag see @c KMEM_RANGE_ID_SPRAYQTN.
 *
 * @const Z_KALLOC_ARRAY
 * Instead of returning a standard "pointer" return a pointer that encodes
 * its size-class into the pointer itself (Only for kalloc, might limit
 * the range of allocations that can be done).
 *
 * @const Z_FULLSIZE
 * Used to indicate that the caller will use all available space in excess
 * from the requested allocation size.
 *
 * @const Z_SKIP_KASAN
 * Tell zalloc() not to do any kasan adjustments.
 *
 * @const Z_MAY_COPYINMAP
 * This data allocation might be used with vm_map_copyin().
 * This allows for those allocations to be associated with a proper VM object.
 *
 * @const Z_VM_TAG_BT_BIT
 * Used to blame allocation accounting on the first kext
 * found in the backtrace of the allocation.
 *
 * @const Z_NOZZC
 * Used internally to mark allocations that will skip zero validation.
 *
 * @const Z_PCPU
 * Used internally for the percpu paths.
 *
 * @const Z_VM_TAG_MASK
 * Represents bits in which a vm_tag_t for the allocation can be passed.
 * (used by kalloc for the zone tagging debugging feature).
 #endif
 */
__options_decl(zalloc_flags_t, uint32_t, {
	// values smaller than 0xff are shared with the M_* flags from BSD MALLOC
	Z_WAITOK        = 0x0000,
	Z_NOWAIT        = 0x0001,
	Z_NOPAGEWAIT    = 0x0002,
	Z_ZERO          = 0x0004,
	Z_REALLOCF      = 0x0008,

	Z_NOFAIL        = 0x8000,

	/* convenient c++ spellings */
	Z_NOWAIT_ZERO          = Z_NOWAIT | Z_ZERO,
	Z_WAITOK_ZERO          = Z_WAITOK | Z_ZERO,
	Z_WAITOK_ZERO_NOFAIL   = Z_WAITOK | Z_ZERO | Z_NOFAIL,

	Z_KPI_MASK             = Z_WAITOK | Z_NOWAIT | Z_NOPAGEWAIT | Z_ZERO,
});

/*
 * This type is used so that kalloc_internal has good calling conventions
 * for callers who want to cheaply both know the allocated address
 * and the actual size of the allocation.
 */
struct kalloc_result {
	void         *addr __sized_by(size);
	vm_size_t     size;
};

/*!
 * @typedef zone_stats_t
 *
 * @abstract
 * The opaque type for per-cpu zone stats that are accumulated per zone
 * or per zone-view.
 */
typedef struct zone_stats *__zpercpu zone_stats_t;

/*!
 * @typedef zone_view_t
 *
 * @abstract
 * A view on a zone for accounting purposes.
 *
 * @discussion
 * A zone view uses the zone it references for the allocations backing store,
 * but does the allocation accounting at the view level.
 *
 * These accounting are surfaced by @b zprint(1) and similar tools,
 * which allow for cheap but finer grained understanding of allocations
 * without any fragmentation cost.
 *
 * Zone views are protected by the kernel lockdown and can't be initialized
 * dynamically. They must be created using @c ZONE_VIEW_DEFINE().
 */
typedef struct zone_view *zone_view_t;
struct zone_view {
	zone_t          zv_zone;
	zone_stats_t    zv_stats;
	const char     *zv_name __unsafe_indexable;
	zone_view_t     zv_next;
};

/*!
 * @typedef kalloc_type_view_t
 *
 * @abstract
 * The opaque type created at kalloc_type callsites to redirect calls to
 * the right zone.
 */
typedef struct kalloc_type_view *kalloc_type_view_t;


extern void *__unsafe_indexable kalloc_type_impl(
	kalloc_type_view_t  kt_view,
	zalloc_flags_t      flags);

static inline void *__unsafe_indexable
__kalloc_type_impl(
	kalloc_type_view_t  kt_view,
	zalloc_flags_t      flags)
{
	void *__unsafe_indexable addr = (kalloc_type_impl)(kt_view, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr;
}

#define kalloc_type_impl(ktv, fl) __kalloc_type_impl(ktv, fl)

extern void kfree_type_impl(
	kalloc_type_view_t  kt_view,
	void                *ptr __unsafe_indexable);


/*!
 * @function zalloc
 *
 * @abstract
 * Allocates an element from a specified zone.
 *
 * @discussion
 * If the zone isn't exhaustible and is expandable, this call never fails.
 *
 * @param zone          the zone or zone view to allocate from
 *
 * @returns             NULL or the allocated element
 */
__attribute__((malloc))
extern void *__unsafe_indexable zalloc(
	zone_t          zone);

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
zalloc(zone_view_t view)
{
	return zalloc((zone_t)view);
}

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
zalloc(kalloc_type_view_t kt_view)
{
	return (kalloc_type_impl)(kt_view, Z_WAITOK);
}

/*!
 * @function zalloc_noblock
 *
 * @abstract
 * Allocates an element from a specified zone, but never blocks.
 *
 * @discussion
 * This call is suitable for preemptible code, however allocation
 * isn't allowed from interrupt context.
 *
 * @param zone          the zone or zone view to allocate from
 *
 * @returns             NULL or the allocated element
 */
__attribute__((malloc))
extern void *__unsafe_indexable zalloc_noblock(
	zone_t          zone);

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
zalloc_noblock(zone_view_t view)
{
	return zalloc_noblock((zone_t)view);
}

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
zalloc_noblock(kalloc_type_view_t kt_view)
{
	return (kalloc_type_impl)(kt_view, Z_NOWAIT);
}

/*!
 * @function zalloc_flags()
 *
 * @abstract
 * Allocates an element from a specified zone, with flags.
 *
 * @param zone          the zone or zone view to allocate from
 * @param flags         a collection of @c zalloc_flags_t.
 *
 * @returns             NULL or the allocated element
 */
__attribute__((malloc))
extern void *__unsafe_indexable zalloc_flags(
	zone_t          zone,
	zalloc_flags_t  flags);

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
__zalloc_flags(
	zone_t          zone,
	zalloc_flags_t  flags)
{
	void *__unsafe_indexable addr = (zalloc_flags)(zone, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr;
}

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
__zalloc_flags(
	zone_view_t     view,
	zalloc_flags_t  flags)
{
	return __zalloc_flags((zone_t)view, flags);
}

__attribute__((malloc))
__attribute__((overloadable))
static inline void *__unsafe_indexable
__zalloc_flags(
	kalloc_type_view_t  kt_view,
	zalloc_flags_t      flags)
{
	void *__unsafe_indexable addr = (kalloc_type_impl)(kt_view, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr;
}

#define zalloc_flags(zov, fl) __zalloc_flags(zov, fl)

/*!
 * @macro zalloc_id
 *
 * @abstract
 * Allocates an element from a specified zone ID, with flags.
 *
 * @param zid           The proper @c ZONE_ID_* constant.
 * @param flags         a collection of @c zalloc_flags_t.
 *
 * @returns             NULL or the allocated element
 */
__attribute__((malloc))
extern void *__unsafe_indexable zalloc_id(
	zone_id_t       zid,
	zalloc_flags_t  flags);

__attribute__((malloc))
static inline void *__unsafe_indexable
__zalloc_id(
	zone_id_t       zid,
	zalloc_flags_t  flags)
{
	void *__unsafe_indexable addr = (zalloc_id)(zid, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr;
}

#define zalloc_id(zid, fl) __zalloc_id(zid, fl)

/*!
 * @function zalloc_ro
 *
 * @abstract
 * Allocates an element from a specified read-only zone.
 *
 * @param zone_id       the zone id to allocate from
 * @param flags         a collection of @c zalloc_flags_t.
 *
 * @returns             NULL or the allocated element
 */
__attribute__((malloc))
extern void *__unsafe_indexable zalloc_ro(
	zone_id_t       zone_id,
	zalloc_flags_t  flags);

__attribute__((malloc))
static inline void *__unsafe_indexable
__zalloc_ro(
	zone_id_t       zone_id,
	zalloc_flags_t  flags)
{
	void *__unsafe_indexable addr = (zalloc_ro)(zone_id, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr;
}

#define zalloc_ro(zid, fl) __zalloc_ro(zid, fl)

/*!
 * @function zalloc_ro_mut
 *
 * @abstract
 * Modifies an element from a specified read-only zone.
 *
 * @discussion
 * Modifying compiler-assisted authenticated pointers using this function will
 * not result in a signed pointer being written.  The caller is expected to
 * sign the value appropriately beforehand if they wish to do this.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param offset        offset from element
 * @param new_data      pointer to new data
 * @param new_data_size size of modification
 *
 */
extern void zalloc_ro_mut(
	zone_id_t       zone_id,
	void           *elem __unsafe_indexable,
	vm_offset_t     offset,
	const void     *new_data __sized_by(new_data_size),
	vm_size_t       new_data_size);

/*!
 * @function zalloc_ro_update_elem
 *
 * @abstract
 * Update the value of an entire element allocated in the read only allocator.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param new_data      pointer to new data
 *
 */
#define zalloc_ro_update_elem(zone_id, elem, new_data)  ({ \
	const typeof(*(elem)) *__new_data = (new_data);                        \
	zalloc_ro_mut(zone_id, elem, 0, __new_data, sizeof(*__new_data));      \
})

/*!
 * @function zalloc_ro_update_field
 *
 * @abstract
 * Update a single field of an element allocated in the read only allocator.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param field         the element field to be modified
 * @param new_data      pointer to new data
 *
 */
#define zalloc_ro_update_field(zone_id, elem, field, value)  ({ \
	const typeof((elem)->field) *__value = (value);                        \
	zalloc_ro_mut(zone_id, elem, offsetof(typeof(*(elem)), field),         \
	    __value, sizeof((elem)->field));                                   \
})

#define ZRO_ATOMIC_LONG(op) ZRO_ATOMIC_##op##_64

/*!
 * @enum zro_atomic_op_t
 *
 * @brief
 * Flags that can be used with @c zalloc_ro_*_atomic to specify the desired
 * atomic operations.
 *
 * @discussion
 * This enum provides all flavors of atomic operations supported in sizes 8,
 * 16, 32, 64 bits.
 *
 * @const ZRO_ATOMIC_OR_*
 * To perform an @s os_atomic_or
 *
 * @const ZRO_ATOMIC_XOR_*
 * To perform an @s os_atomic_xor
 *
 * @const ZRO_ATOMIC_AND_*
 * To perform an @s os_atomic_and
 *
 * @const ZRO_ATOMIC_ADD_*
 * To perform an @s os_atomic_add
 *
 * @const ZRO_ATOMIC_XCHG_*
 * To perform an @s os_atomic_xchg
 *
 */
__enum_decl(zro_atomic_op_t, uint32_t, {
	ZRO_ATOMIC_OR_8      = 0x00000010 | 1,
	ZRO_ATOMIC_OR_16     = 0x00000010 | 2,
	ZRO_ATOMIC_OR_32     = 0x00000010 | 4,
	ZRO_ATOMIC_OR_64     = 0x00000010 | 8,

	ZRO_ATOMIC_XOR_8     = 0x00000020 | 1,
	ZRO_ATOMIC_XOR_16    = 0x00000020 | 2,
	ZRO_ATOMIC_XOR_32    = 0x00000020 | 4,
	ZRO_ATOMIC_XOR_64    = 0x00000020 | 8,

	ZRO_ATOMIC_AND_8     = 0x00000030 | 1,
	ZRO_ATOMIC_AND_16    = 0x00000030 | 2,
	ZRO_ATOMIC_AND_32    = 0x00000030 | 4,
	ZRO_ATOMIC_AND_64    = 0x00000030 | 8,

	ZRO_ATOMIC_ADD_8     = 0x00000040 | 1,
	ZRO_ATOMIC_ADD_16    = 0x00000040 | 2,
	ZRO_ATOMIC_ADD_32    = 0x00000040 | 4,
	ZRO_ATOMIC_ADD_64    = 0x00000040 | 8,

	ZRO_ATOMIC_XCHG_8    = 0x00000050 | 1,
	ZRO_ATOMIC_XCHG_16   = 0x00000050 | 2,
	ZRO_ATOMIC_XCHG_32   = 0x00000050 | 4,
	ZRO_ATOMIC_XCHG_64   = 0x00000050 | 8,

	/* cconvenient spellings */
	ZRO_ATOMIC_OR_LONG   = ZRO_ATOMIC_LONG(OR),
	ZRO_ATOMIC_XOR_LONG  = ZRO_ATOMIC_LONG(XOR),
	ZRO_ATOMIC_AND_LONG  = ZRO_ATOMIC_LONG(AND),
	ZRO_ATOMIC_ADD_LONG  = ZRO_ATOMIC_LONG(ADD),
	ZRO_ATOMIC_XCHG_LONG = ZRO_ATOMIC_LONG(XCHG),
});

/*!
 * @function zalloc_ro_mut_atomic
 *
 * @abstract
 * Atomically update an offset in an element allocated in the read only
 * allocator. Do not use directly. Use via @c zalloc_ro_update_field_atomic.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param offset        offset in the element to be modified
 * @param op            atomic operation to perform (see @c zro_atomic_op_t)
 * @param value         value for the atomic operation
 *
 */
extern uint64_t zalloc_ro_mut_atomic(
	zone_id_t       zone_id,
	void           *elem __unsafe_indexable,
	vm_offset_t     offset,
	zro_atomic_op_t op,
	uint64_t        value);

/*!
 * @macro zalloc_ro_update_field_atomic
 *
 * @abstract
 * Atomically update a single field of an element allocated in the read only
 * allocator.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param field         the element field to be modified
 * @param op            atomic operation to perform (see @c zro_atomic_op_t)
 * @param value         value for the atomic operation
 *
 */
#define zalloc_ro_update_field_atomic(zone_id, elem, field, op, value)  ({ \
	const typeof((elem)->field) __value = (value);                         \
	static_assert(sizeof(__value) == (op & 0xf));                          \
	(os_atomic_basetypeof(&(elem)->field))zalloc_ro_mut_atomic(zone_id,    \
	    elem, offsetof(typeof(*(elem)), field), op, (uint64_t)__value);    \
})

/*!
 * @function zalloc_ro_clear
 *
 * @abstract
 * Zeroes an element from a specified read-only zone.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param offset        offset from element
 * @param size          size of modification
 */
extern void    zalloc_ro_clear(
	zone_id_t       zone_id,
	void           *elem __unsafe_indexable,
	vm_offset_t     offset,
	vm_size_t       size);

/*!
 * @function zalloc_ro_clear_field
 *
 * @abstract
 * Zeroes the specified field of an element from a specified read-only zone.
 *
 * @param zone_id       the zone id to allocate from
 * @param elem          element to be modified
 * @param field         offset from element
 */
#define zalloc_ro_clear_field(zone_id, elem, field) \
	zalloc_ro_clear(zone_id, elem, offsetof(typeof(*(elem)), field), \
	    sizeof((elem)->field))

/*!
 * @function zfree_id()
 *
 * @abstract
 * Frees an element previously allocated with @c zalloc_id().
 *
 * @param zone_id       the zone id to free the element to.
 * @param addr          the address to free
 */
extern void     zfree_id(
	zone_id_t       zone_id,
	void           *addr __unsafe_indexable);
#define zfree_id(zid, elem) ({ \
	zone_id_t __zfree_zid = (zid); \
	(zfree_id)(__zfree_zid, (void *)os_ptr_load_and_erase(elem)); \
})


/*!
 * @function zfree_ro()
 *
 * @abstract
 * Frees an element previously allocated with @c zalloc_ro().
 *
 * @param zone_id       the zone id to free the element to.
 * @param addr          the address to free
 */
extern void     zfree_ro(
	zone_id_t       zone_id,
	void           *addr __unsafe_indexable);
#define zfree_ro(zid, elem) ({ \
	zone_id_t __zfree_zid = (zid); \
	(zfree_ro)(__zfree_zid, (void *)os_ptr_load_and_erase(elem)); \
})


/*!
 * @function zfree
 *
 * @abstract
 * Frees an element allocated with @c zalloc*.
 *
 * @discussion
 * If the element being freed doesn't belong to the specified zone,
 * then this call will panic.
 *
 * @param zone          the zone or zone view to free the element to.
 * @param elem          the element to free
 */
extern void     zfree(
	zone_t          zone,
	void           *elem __unsafe_indexable);

__attribute__((overloadable))
static inline void
zfree(
	zone_view_t     view,
	void           *elem __unsafe_indexable)
{
	zfree((zone_t)view, elem);
}

__attribute__((overloadable))
static inline void
zfree(
	kalloc_type_view_t   kt_view,
	void                *elem __unsafe_indexable)
{
	return kfree_type_impl(kt_view, elem);
}

#define zfree(zone, elem) ({ \
	__auto_type __zfree_zone = (zone); \
	(zfree)(__zfree_zone, (void *)os_ptr_load_and_erase(elem)); \
})


/* deprecated KPIS */

__zalloc_deprecated("use zone_create()")
extern zone_t   zinit(
	vm_size_t       size,           /* the size of an element */
	vm_size_t       maxmem,         /* maximum memory to use */
	vm_size_t       alloc,          /* allocation size */
	const char      *name __unsafe_indexable);

#pragma mark: implementation details

#define __ZONE_DECLARE_TYPE(var, type_t) __ZONE_DECLARE_TYPE2(var, type_t)
#define __ZONE_DECLARE_TYPE2(var, type_t) \
	__attribute__((visibility("hidden"))) \
	extern type_t *__single __zalloc__##var##__type_name


/*
 * This macro is currently used by AppleImage4 (rdar://83924635)
 */
#define __zalloc_ptr_load_and_erase(elem) \
	os_ptr_load_and_erase(elem)

__ASSUME_PTR_ABI_SINGLE_END __END_DECLS

#endif  /* _KERN_ZALLOC_H_ */

