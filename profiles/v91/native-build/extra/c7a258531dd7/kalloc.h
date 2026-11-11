/*
 * Copyright (c) 2000-2021 Apple Computer, Inc. All rights reserved.
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


#ifndef _KERN_KALLOC_H_
#define _KERN_KALLOC_H_

#include <mach/machine/vm_types.h>
#include <mach/boolean.h>
#include <mach/vm_types.h>
#include <kern/zalloc.h>
#include <libkern/section_keywords.h>
#include <os/alloc_util.h>

__BEGIN_DECLS __ASSUME_PTR_ABI_SINGLE_BEGIN

/*!
 * @const KALLOC_SAFE_ALLOC_SIZE
 *
 * @brief
 * The maximum allocation size that is safe to allocate with Z_NOFAIL in kalloc.
 */
#define KALLOC_SAFE_ALLOC_SIZE  (16u * 1024u)


/*!
 * @enum kalloc_type_flags_t
 *
 * @brief
 * Flags that can be passed to @c KALLOC_TYPE_DEFINE
 *
 * @discussion
 * These flags can be used to request for a specific accounting
 * behavior.
 *
 * @const KT_DEFAULT
 * Passing this flag will provide default accounting behavior
 * i.e shared accounting unless toggled with KT_OPTIONS_ACCT is
 * set in kt boot-arg.
 *
 * @const KT_PRIV_ACCT
 * Passing this flag will provide individual stats for your
 * @c kalloc_type_view that is defined.
 *
 * @const KT_SHARED_ACCT
 * Passing this flag will accumulate stats as a part of the
 * zone that your @c kalloc_type_view points to.
 *
 * @const KT_DATA_ONLY
 * Represents that the type is "data-only". Adopters should not
 * set this flag manually, it is meant for the compiler to set
 * automatically when KALLOC_TYPE_CHECK(DATA) passes.
 *
 * @const KT_VM
 * Represents that the type is large enough to use the VM. Adopters
 * should not set this flag manually, it is meant for the compiler
 * to set automatically when KALLOC_TYPE_VM_SIZE_CHECK passes.
 *
 * @const KT_PTR_ARRAY
 * Represents that the type is an array of pointers. Adopters should not
 * set this flag manually, it is meant for the compiler to set
 * automatically when KALLOC_TYPE_CHECK(PTR) passes.
 *
 * @const KT_CHANGED*
 * Represents a change in the version of the kalloc_type_view. This
 * is required inorder to decouple requiring kexts to be rebuilt to
 * use the new defintions right away. This flags should not be used
 * manually at a callsite, it is meant for internal use only. Future
 * changes to kalloc_type_view defintion should toggle this flag.
 *
 #if XNU_KERNEL_PRIVATE
 * @const KT_NOSHARED
 * This flags will force the callsite to bypass the shared zone and
 * directly allocate from the assigned zone. This can only be used
 * with KT_PRIV_ACCT right now. If you still require this behavior
 * but don't want private stats use Z_SET_NOTSHARED at the allocation
 * callsite instead.
 *
 * @const KT_SLID
 * To indicate that strings in the view were slid during early boot.
 *
 * @const KT_PROCESSED
 * This flag is set once the view is parse during early boot. Views
 * that are not in BootKC on macOS aren't parsed and therefore will
 * not have this flag set. The runtime can use this as an indication
 * to appropriately redirect the call.
 *
 * @const KT_HASH
 * Hash of signature used by kmem_*_guard to determine range and
 * direction for allocation
 #endif
 */
__options_decl(kalloc_type_flags_t, uint32_t, {
	KT_DEFAULT        = 0x0001,
	KT_PRIV_ACCT      = 0x0002,
	KT_SHARED_ACCT    = 0x0004,
	KT_DATA_ONLY      = 0x0008,
	KT_VM             = 0x0010,
	KT_CHANGED        = 0x0020,
	KT_CHANGED2       = 0x0040,
	KT_PTR_ARRAY      = 0x0080,
});

/*!
 * @typedef kalloc_type_view_t
 *
 * @abstract
 * A kalloc type view is a structure used to redirect callers
 * of @c kalloc_type to a particular zone based on the signature of
 * their type.
 *
 * @discussion
 * These structures are automatically created under the hood for every
 * @c kalloc_type and @c kfree_type callsite. They are ingested during startup
 * and are assigned zones based on the security policy for their signature.
 *
 * These structs are protected by the kernel lockdown and can't be initialized
 * dynamically. They must be created using @c KALLOC_TYPE_DEFINE() or
 * @c kalloc_type or @c kfree_type.
 *
 */
struct kalloc_type_view {
	struct zone_view        kt_zv;
	const char             *kt_signature __unsafe_indexable;
	kalloc_type_flags_t     kt_flags;
	uint32_t                kt_size;
	void                   *unused1;
	void                   *unused2;
};

/*
 * The set of zones used by all kalloc heaps are defined by the constants
 * below.
 *
 * KHEAP_START_SIZE: Size of the first sequential zone.
 * KHEAP_MAX_SIZE  : Size of the last sequential zone.
 * KHEAP_STEP_WIDTH: Number of zones created at every step (power of 2).
 * KHEAP_STEP_START: Size of the first step.
 * We also create some extra initial zones that don't follow the sequence
 * for sizes 8 (on armv7 only), 16 and 32.
 *
 * idx step_increment   zone_elem_size
 * 0       -                  16
 * 1       -                  32
 * 2       16                 48
 * 3       16                 64
 * 4       32                 96
 * 5       32                 128
 * 6       64                 192
 * 7       64                 256
 * 8       128                384
 * 9       128                512
 * 10      256                768
 * 11      256                1024
 * 12      512                1536
 * 13      512                2048
 * 14      1024               3072
 * 15      1024               4096
 * 16      2048               6144
 * 17      2048               8192
 * 18      4096               12288
 * 19      4096               16384
 * 20      8192               24576
 * 21      8192               32768
 */
#define kalloc_log2down(mask)   (31 - __builtin_clz(mask))
#define KHEAP_START_SIZE        32
#if  __x86_64__
#define KHEAP_MAX_SIZE          (16 * 1024)
#define KHEAP_EXTRA_ZONES       2
#else
#define KHEAP_MAX_SIZE          (32 * 1024)
#define KHEAP_EXTRA_ZONES       2
#endif
#define KHEAP_STEP_WIDTH        2
#define KHEAP_STEP_START        16
#define KHEAP_START_IDX         kalloc_log2down(KHEAP_START_SIZE)
#define KHEAP_NUM_STEPS         (kalloc_log2down(KHEAP_MAX_SIZE) - \
	                                kalloc_log2down(KHEAP_START_SIZE))
#define KHEAP_NUM_ZONES         (KHEAP_NUM_STEPS * KHEAP_STEP_WIDTH + \
	                                KHEAP_EXTRA_ZONES)

/*!
 * @enum kalloc_type_version_t
 *
 * @brief
 * Enum that holds versioning information for @c kalloc_type_var_view
 *
 * @const KT_V1
 * Version 1
 *
 */
__options_decl(kalloc_type_version_t, uint16_t, {
	KT_V1             = 0x0001,
});

/*!
 * @typedef kalloc_type_var_view_t
 *
 * @abstract
 * This structure is analoguous to @c kalloc_type_view but handles
 * @c kalloc_type callsites that are variable in size.
 *
 * @discussion
 * These structures are automatically created under the hood for every
 * variable sized @c kalloc_type and @c kfree_type callsite. They are ingested
 * during startup and are assigned zones based on the security policy for
 * their signature.
 *
 * These structs are protected by the kernel lockdown and can't be initialized
 * dynamically. They must be created using @c KALLOC_TYPE_VAR_DEFINE() or
 * @c kalloc_type or @c kfree_type.
 *
 */
struct kalloc_type_var_view {
	kalloc_type_version_t   kt_version;
	uint16_t                kt_size_hdr;
	/*
	 * Temporary: Needs to be 32bits cause we have many structs that use
	 * IONew/Delete that are larger than 32K.
	 */
	uint32_t                kt_size_type;
	zone_stats_t            kt_stats;
	const char             *__unsafe_indexable kt_name;
	zone_view_t             kt_next;
	zone_id_t               kt_heap_start;
	uint8_t                 kt_zones[KHEAP_NUM_ZONES];
	const char             * __unsafe_indexable kt_sig_hdr;
	const char             * __unsafe_indexable kt_sig_type;
	kalloc_type_flags_t     kt_flags;
};

typedef struct kalloc_type_var_view *kalloc_type_var_view_t;

/*!
 * @macro KALLOC_TYPE_DECLARE
 *
 * @abstract
 * (optionally) declares a kalloc type view (in a header).
 *
 * @param var           the name for the kalloc type view.
 */
#define KALLOC_TYPE_DECLARE(var) \
	extern struct kalloc_type_view var[1]

/*!
 * @macro KALLOC_TYPE_DEFINE
 *
 * @abstract
 * Defines a given kalloc type view with prefered accounting
 *
 * @discussion
 * This macro allows you to define a kalloc type with private
 * accounting. The defined kalloc_type_view can be used with
 * kalloc_type_impl/kfree_type_impl to allocate/free memory.
 * zalloc/zfree can also be used from inside xnu. However doing
 * so doesn't handle freeing a NULL pointer or the use of tags.
 *
 * @param var           the name for the kalloc type view.
 * @param type          the type of your allocation.
 * @param flags         a @c KT_* flag.
 */
#define KALLOC_TYPE_DEFINE(var, type, flags) \
	_KALLOC_TYPE_DEFINE(var, type, flags); \
	__ZONE_DECLARE_TYPE(var, type)

/*!
 * @macro KALLOC_TYPE_VAR_DECLARE
 *
 * @abstract
 * (optionally) declares a kalloc type var view (in a header).
 *
 * @param var           the name for the kalloc type var view.
 */
#define KALLOC_TYPE_VAR_DECLARE(var) \
	extern struct kalloc_type_var_view var[1]

/*!
 * @macro KALLOC_TYPE_VAR_DEFINE
 *
 * @abstract
 * Defines a given kalloc type view with prefered accounting for
 * variable sized typed allocations.
 *
 * @discussion
 * As the views aren't yet being ingested, individual stats aren't
 * available. The defined kalloc_type_var_view should be used with
 * kalloc_type_var_impl/kfree_type_var_impl to allocate/free memory.
 *
 * This macro comes in 2 variants:
 *
 * 1. @c KALLOC_TYPE_VAR_DEFINE(var, e_ty, flags)
 * 2. @c KALLOC_TYPE_VAR_DEFINE(var, h_ty, e_ty, flags)
 *
 * @param var           the name for the kalloc type var view.
 * @param h_ty          the type of header in the allocation.
 * @param e_ty          the type of repeating part in the allocation.
 * @param flags         a @c KT_* flag.
 */
#define KALLOC_TYPE_VAR_DEFINE(...) KALLOC_DISPATCH(KALLOC_TYPE_VAR_DEFINE, ##__VA_ARGS__)


extern void *__sized_by(size) kalloc(
	vm_size_t           size) __attribute__((malloc, alloc_size(1)));

extern void *__unsafe_indexable kalloc_data(
	vm_size_t           size,
	zalloc_flags_t      flags);

__attribute__((malloc, alloc_size(1)))
static inline void *
__sized_by(size)
__kalloc_data(vm_size_t size, zalloc_flags_t flags)
{
	void *__unsafe_indexable addr = (kalloc_data)(size, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr ? __unsafe_forge_bidi_indexable(uint8_t *, addr, size) : NULL;
}

#define kalloc_data(size, fl) __kalloc_data(size, fl)

extern void *__unsafe_indexable krealloc_data(
	void               *ptr __unsafe_indexable,
	vm_size_t           old_size,
	vm_size_t           new_size,
	zalloc_flags_t      flags);

__attribute__((malloc, alloc_size(3)))
static inline void *
__sized_by(new_size)
__krealloc_data(
	void               *ptr __sized_by(old_size),
	vm_size_t           old_size,
	vm_size_t           new_size,
	zalloc_flags_t      flags)
{
	void *__unsafe_indexable addr = (krealloc_data)(ptr, old_size, new_size, flags);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return addr ? __unsafe_forge_bidi_indexable(uint8_t *, addr, new_size) : NULL;
}

#define krealloc_data(ptr, old_size, new_size, fl) \
	__krealloc_data(ptr, old_size, new_size, fl)

extern void kfree(
	void               *data __unsafe_indexable,
	vm_size_t           size);

extern void kfree_data(
	void               *ptr __unsafe_indexable,
	vm_size_t           size);

extern void kfree_data_addr(
	void               *ptr __unsafe_indexable);


/*!
 * @macro kalloc_type
 *
 * @abstract
 * Allocates element of a particular type
 *
 * @discussion
 * This family of allocators segregate kalloc allocations based on their type.
 *
 * This macro comes in 3 variants:
 *
 * 1. @c kalloc_type(type, flags)
 *    Use this macro for fixed sized allocation of a particular type.
 *
 * 2. @c kalloc_type(e_type, count, flags)
 *    Use this macro for variable sized allocations that form an array,
 *    do note that @c kalloc_type(e_type, 1, flags) is not equivalent to
 *    @c kalloc_type(e_type, flags).
 *
 * 3. @c kalloc_type(hdr_type, e_type, count, flags)
 *    Use this macro for variable sized allocations formed with
 *    a header of type @c hdr_type followed by a variable sized array
 *    with elements of type @c e_type, equivalent to this:
 *
 *    <code>
 *    struct {
 *        hdr_type hdr;
 *        e_type   arr[];
 *    }
 *    </code>
 *
 * @param flags         @c zalloc_flags_t that get passed to zalloc_internal
 */
#define kalloc_type(...)  KALLOC_DISPATCH(kalloc_type, ##__VA_ARGS__)

/*!
 * @macro kfree_type
 *
 * @abstract
 * Allocates element of a particular type
 *
 * @discussion
 * This pairs with the @c kalloc_type() that was made to allocate this element.
 * Arguments passed to @c kfree_type() must match the one passed at allocation
 * time precisely.
 *
 * This macro comes in the same 3 variants kalloc_type() does:
 *
 * 1. @c kfree_type(type, elem)
 * 2. @c kfree_type(e_type, count, elem)
 * 3. @c kfree_type(hdr_type, e_type, count, elem)
 *
 * @param elem          The address of the element to free
 */
#define kfree_type(...)  KALLOC_DISPATCH(kfree_type, ##__VA_ARGS__)
#define kfree_type_counted_by(type, count, elem) \
	kfree_type_counted_by_3(type, count, elem)


/*!
 * @enum kt_granule_t
 *
 * @brief
 * Granule encodings used by the compiler for the type signature.
 *
 * @discussion
 * Given a type, the XNU signature type system (__builtin_xnu_type_signature)
 * produces a signature by analyzing its memory layout, in chunks of 8 bytes,
 * which we call granules. The encoding produced for each granule is the
 * bitwise or of the encodings of all the types of the members included
 * in that granule.
 *
 * @const KT_GRANULE_PADDING
 * Represents padding inside a record type.
 *
 * @const KT_GRANULE_POINTER
 * Represents a pointer type.
 *
 * @const KT_GRANULE_DATA
 * Represents a scalar type that is not a pointer.
 *
 * @const KT_GRANULE_DUAL
 * Currently unused.
 *
 * @const KT_GRANULE_PAC
 * Represents a pointer which is subject to PAC.
 */
__options_decl(kt_granule_t, uint32_t, {
	KT_GRANULE_PADDING = 0,
	KT_GRANULE_POINTER = 1,
	KT_GRANULE_DATA    = 2,
	KT_GRANULE_DUAL    = 4,
	KT_GRANULE_PAC     = 8
});

#define KT_GRANULE_MAX                                                \
	(KT_GRANULE_PADDING | KT_GRANULE_POINTER | KT_GRANULE_DATA |  \
	    KT_GRANULE_DUAL | KT_GRANULE_PAC)

/*
 * Convert a granule encoding to the index of the bit that
 * represents such granule in the type summary.
 *
 * The XNU type summary (__builtin_xnu_type_summary) produces a 32-bit
 * summary of the type signature of a given type. If the bit at index
 * (1 << G) is set in the summary, that means that the type contains
 * one or more granules with encoding G.
 */
#define KT_SUMMARY_GRANULE_TO_IDX(g)  (1UL << (g))

#define KT_SUMMARY_MASK_TYPE_BITS  (0xffff)

#define KT_SUMMARY_MASK_DATA                             \
	(KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_PADDING) |  \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_DATA))

#define KT_SUMMARY_MASK_PTR                              \
	(KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_PADDING) |     \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_POINTER) |  \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_PAC))

#define KT_SUMMARY_MASK_ALL_GRANULES                        \
	(KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_PADDING) |     \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_POINTER) |  \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_DATA) |     \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_DUAL) |     \
	    KT_SUMMARY_GRANULE_TO_IDX(KT_GRANULE_PAC))

/*!
 * @macro KT_SUMMARY_GRANULES
 *
 * @abstract
 * Return the granule type summary for a given type
 *
 * @discussion
 * This macro computes the type summary of a type, and it then extracts the
 * bits which carry information about the granules in the memory layout.
 *
 * Note: you should never have to use __builtin_xnu_type_summary
 * directly, as we reserve the right to use the remaining bits with
 * different semantics.
 *
 * @param type          The type to analyze
 */
#define KT_SUMMARY_GRANULES(type) \
	(__builtin_xnu_type_summary(type) & KT_SUMMARY_MASK_TYPE_BITS)

/*!
 * @macro KALLOC_TYPE_SIG_CHECK
 *
 * @abstract
 * Return whether a given type is only made up of granules specified in mask
 *
 * @param mask          Granules to check for
 * @param type          The type to analyze
 */
#define KALLOC_TYPE_SIG_CHECK(mask, type) \
	((KT_SUMMARY_GRANULES(type) & ~(mask)) == 0)

/*!
 * @macro KALLOC_TYPE_IS_DATA_ONLY
 *
 * @abstract
 * Return whether a given type is considered a data-only type.
 *
 * @param type          The type to analyze
 */
#define KALLOC_TYPE_IS_DATA_ONLY(type) \
	KALLOC_TYPE_SIG_CHECK(KT_SUMMARY_MASK_DATA, type)

/*!
 * @macro KALLOC_TYPE_HAS_OVERLAPS
 *
 * @abstract
 * Return whether a given type has overlapping granules.
 *
 * @discussion
 * This macro returns whether the memory layout for a given type contains
 * overlapping granules. An overlapping granule is a granule which includes
 * members with types that have different encodings under the XNU signature
 * type system.
 *
 * @param type          The type to analyze
 */
#define KALLOC_TYPE_HAS_OVERLAPS(type) \
	((KT_SUMMARY_GRANULES(type) & ~KT_SUMMARY_MASK_ALL_GRANULES) != 0)

/*!
 * @macro KALLOC_TYPE_IS_COMPATIBLE_PTR
 *
 * @abstract
 * Return whether pointer is compatible with a given type, in the XNU
 * signature type system.
 *
 * @discussion
 * This macro returns whether type pointed to by @c ptr is either the same
 * type as @c type, or it has the same signature. The implementation relies
 * on the @c __builtin_xnu_types_compatible builtin, and the value returned
 * can be evaluated at compile time in both C and C++.
 *
 * Note: void pointers are treated as wildcards, and are thus compatible
 * with any given type.
 *
 * @param ptr           the pointer whose type needs to be checked.
 * @param type          the type which the pointer will be checked against.
 */
#define KALLOC_TYPE_IS_COMPATIBLE_PTR(ptr, type)                         \
	(__builtin_xnu_types_compatible(os_get_pointee_type(ptr), type) ||   \
	    __builtin_xnu_types_compatible(os_get_pointee_type(ptr), void))  \

#define KALLOC_TYPE_ASSERT_COMPATIBLE_POINTER(ptr, type) \
	_Static_assert(KALLOC_TYPE_IS_COMPATIBLE_PTR(ptr, type), \
	    "Pointer type is not compatible with specified type")


/*!
 * @const KALLOC_ARRAY_SIZE_MAX
 *
 * @brief
 * The maximum size that can be allocated with the @c KALLOC_ARRAY interface.
 *
 * @discussion
 * This size is:
 * - ~256M on 4k or PAC systems with 16k pages
 * - ~1G on other 16k systems.
 */
#if __arm64e__ || KASAN_TBI
#define KALLOC_ARRAY_SIZE_MAX   ((uint32_t)PAGE_MASK << PAGE_SHIFT)
#define KALLOC_ARRAY_GRANULE    32ul
#else
#define KALLOC_ARRAY_SIZE_MAX   ((uint32_t)UINT16_MAX << PAGE_SHIFT)
#define KALLOC_ARRAY_GRANULE    16ul
#endif

/*!
 * @macro KALLOC_ARRAY_TYPE_DECL
 *
 * @brief
 * Declares a type used as a packed kalloc array type.
 *
 * @discussion
 * This macro comes in two variants
 *
 * - KALLOC_ARRAY_TYPE_DECL(name, e_ty)
 * - KALLOC_ARRAY_TYPE_DECL(name, h_ty, e_ty)
 *
 * The first one defines an array of elements of type @c e_ty,
 * and the second a header of type @c h_ty followed by
 * an array of elements of type @c e_ty.
 *
 * Those macros will then define the type @c ${name}_t as a typedef
 * to a non existent structure type, in order to avoid accidental
 * dereference of those pointers.
 *
 * kalloc array pointers are actually pointers that in addition to encoding
 * the array base pointer, also encode the allocation size (only sizes
 * up to @c KALLOC_ARRAY_SIZE_MAX bytes).
 *
 * Such pointers can be signed with data PAC properly, which will provide
 * integrity of both the base pointer, and its size.
 *
 * kalloc arrays are useful to use instead of embedding the length
 * of the allocation inside of itself, which tends to be driven by:
 *
 * - a desire to not grow the outer structure holding the pointer
 *   to this array with an extra "length" field for optional arrays,
 *   in order to save memory (see the @c ip_requests field in ports),
 *
 * - a need to be able to atomically consult the size of an allocation
 *   with respect to loading its pointer (where address dependencies
 *   traditionally gives this property) for lockless algorithms
 *   (see the IPC space table).
 *
 * Using a kalloc array is preferable for two reasons:
 *
 * - embedding lengths inside the allocation is self-referential
 *   and an appetizing target for post-exploitation strategies,
 *
 * - having a dependent load to get to the length loses out-of-order
 *   opportunities for the CPU and prone to back-to-back cache misses.
 *
 * Holding information such as a level of usage of this array
 * within itself is fine provided those quantities are validated
 * against the "count" (number of elements) or "size" (allocation
 * size in bytes) of the array before use.
 *
 *
 * This macro will define a series of functions:
 *
 * - ${name}_count_to_size() and ${name}_size_to_count()
 *   to convert between memory sizes and array element counts
 *   (taking the header size into account when it exists);
 *
 *   Note that those functions assume the count/size are corresponding
 *   to a valid allocation size within [0, KALLOC_ARRAY_SIZE_MAX].
 *
 * - ${name}_next_size() to build good allocation growth policies;
 *
 * - ${name}_base() returning a (bound-checked indexable) pointer
 *   to the header of the array (or its first element when there is
 *   no header);
 *
 * - ${name}_begin() returning a (bound-checked indexable)
 *   pointer to the first element of the the array;
 *
 * - ${name}_contains() to check if an element index is within
 *   the valid range of this allocation;
 *
 * - ${name}_next_elem() to get the next element of an array.
 *
 * - ${name}_get() and ${name}_get_nocheck() to return a pointer
 *   to a given cell of the array with (resp. without) a bound
 *   check against the array size. The bound-checked variant
 *   returns NULL for invalid indexes.
 *
 * - ${name}_alloc_by_count() and ${name}_alloc_by_size()
 *   to allocate a new array able to hold at least that many elements
 *   (resp. bytes).
 *
 * - ${name}_realloc_by_count() and ${name}_realloc_by_size()
 *   to re-allocate a new array able to hold at least that many elements
 *   (resp. bytes).
 *
 * - ${name}_free() and ${name}_free_noclear() to free such an array
 *   (resp. without nil-ing the pointer). The non-clearing variant
 *   is to be used only when nil-ing out the pointer is otherwise
 *   not allowed by C (const value, unable to take address of, ...),
 *   otherwise the normal ${name}_free() must be used.
 */
#define KALLOC_ARRAY_TYPE_DECL(...) \
	KALLOC_DISPATCH(KALLOC_ARRAY_TYPE_DECL, ##__VA_ARGS__)


#define KALLOC_ARRAY_TYPE_DECL_(name, h_type_t, h_sz, e_type_t, e_sz) \
	typedef struct name * __unsafe_indexable name ## _t

#pragma mark implementation details


static inline void *__unsafe_indexable
kt_mangle_var_view(kalloc_type_var_view_t kt_view)
{
	return (void *__unsafe_indexable)((uintptr_t)kt_view | 1ul);
}

static inline kalloc_type_var_view_t __unsafe_indexable
kt_demangle_var_view(void *ptr)
{
	return (kalloc_type_var_view_t __unsafe_indexable)((uintptr_t)ptr & ~1ul);
}

#define kt_is_var_view(ptr)  ((uintptr_t)(ptr) & 1)

#define kt_realign_sizeof(h_ty, e_ty) \
	((sizeof(h_ty) + _Alignof(e_ty) - 1) & -_Alignof(e_ty))

static inline vm_size_t
kt_size(vm_size_t s1, vm_size_t s2, vm_size_t c2)
{
	/* kalloc_large() will reject this size before even asking the VM  */
	const vm_size_t limit = 1ull << (8 * sizeof(vm_size_t) - 1);

	if (os_mul_and_add_overflow(s2, c2, s1, &s1) || (s1 & limit)) {
		return limit;
	}
	return s1;
}

#define kalloc_type_2(type, flags) ({                                          \
	static _KALLOC_TYPE_DEFINE(kt_view_var, type, KT_SHARED_ACCT);         \
	__unsafe_forge_single(type *, kalloc_type_impl(kt_view_var, flags));   \
})

#define kfree_type_2(type, elem) ({                                            \
	KALLOC_TYPE_ASSERT_COMPATIBLE_POINTER(elem, type);                     \
	static _KALLOC_TYPE_DEFINE(kt_view_var, type, KT_SHARED_ACCT);         \
	kfree_type_impl(kt_view_var, os_ptr_load_and_erase(elem));             \
})

#define kfree_type_3(type, count, elem) ({                                     \
	KALLOC_TYPE_ASSERT_COMPATIBLE_POINTER(elem, type);                     \
	static KALLOC_TYPE_VAR_DEFINE_3(kt_view_var, type, KT_SHARED_ACCT);    \
	__auto_type __kfree_count = (count);                                   \
	kfree_type_var_impl(kt_view_var, os_ptr_load_and_erase(elem),          \
	    kt_size(0, sizeof(type), __kfree_count));                          \
})

#define kfree_type_counted_by_3(type, count_var, elem_var) ({              \
	void *__bidi_indexable __elem_copy = (elem_var);                       \
	__auto_type __kfree_count = (count_var);                               \
	(elem_var) = 0;                                                        \
	(count_var) = 0;                                                       \
	KALLOC_TYPE_ASSERT_COMPATIBLE_POINTER(__elem_copy, type);              \
	static KALLOC_TYPE_VAR_DEFINE_3(kt_view_var, type, KT_SHARED_ACCT);    \
	kfree_type_var_impl(kt_view_var, __elem_copy,                          \
	    kt_size(0, sizeof(type), __kfree_count));                          \
})

#define kfree_type_4(hdr_ty, e_ty, count, elem) ({                             \
	KALLOC_TYPE_ASSERT_COMPATIBLE_POINTER(elem, hdr_ty);                   \
	static KALLOC_TYPE_VAR_DEFINE_4(kt_view_var, hdr_ty, e_ty,             \
	    KT_SHARED_ACCT);                                                   \
	__auto_type __kfree_count = (count);                                   \
	kfree_type_var_impl(kt_view_var,                                       \
	    os_ptr_load_and_erase(elem),                                       \
	    kt_size(kt_realign_sizeof(hdr_ty, e_ty), sizeof(e_ty),             \
	    __kfree_count));                                                   \
})


#define kalloc_type_3(type, count, flags) ({                                   \
	static KALLOC_TYPE_VAR_DEFINE_3(kt_view_var, type, KT_SHARED_ACCT);    \
	(type *)kalloc_type_var_impl(kt_view_var,                              \
	    kt_size(0, sizeof(type), count), flags, NULL);                     \
})

#define kalloc_type_4(hdr_ty, e_ty, count, flags) ({                           \
	static KALLOC_TYPE_VAR_DEFINE_4(kt_view_var, hdr_ty, e_ty,             \
	    KT_SHARED_ACCT);                                                   \
	(hdr_ty *)kalloc_type_var_impl(kt_view_var,                            \
	    kt_size(kt_realign_sizeof(hdr_ty, e_ty), sizeof(e_ty), count),     \
	    flags, NULL);                                                      \
})


/*
 * All k*free macros set "elem" to NULL on free.
 *
 * Note: all values passed to k*free() might be in the element to be freed,
 *       temporaries must be taken, and the resetting to be done prior to free.
 */

#define kfree_data(elem, size) ({                                              \
	__auto_type __kfree_size = (size);                                     \
	(kfree_data)((void *)os_ptr_load_and_erase(elem), __kfree_size);       \
})

#define kfree_data_addr(elem) \
	(kfree_data_addr)((void *)os_ptr_load_and_erase(elem))


#define __kfree_data_elem_count_size(elem_var, count_var, size) ({             \
	void *__bidi_indexable __elem_copy = (elem_var);                           \
	(elem_var) = 0;                                                            \
	(count_var) = 0;                                                           \
	kfree_data(__elem_copy, size);                                             \
})

/*
 * kfree_data_sized_by is the kfree_data equivalent that is compatible with
 * -fbounds-safety's __sized_by pointers. Consistently with the -fbounds-safety
 * semantics, `size` must be the byte size of the allocation that is freed (for
 * instance, 20 for an array of 5 uint32_t).
 */
#define kfree_data_sized_by(elem, size) ({                                     \
	__auto_type __size = (size);                                               \
	__kfree_data_elem_count_size(elem, size, __size);                          \
})

/*
 * kfree_data_counted_by is the kfree_data equivalent that is compatible with
 * -fbounds-safety's __counted_by pointers. Consistently with the
 * -fbounds-safety semantics, `count` must be the object count of the allocation
 * that is freed (for instance, 5 for an array of 5 uint32_t).
 */
#define kfree_data_counted_by(elem, count) ({                                  \
	__auto_type __size = (count) * sizeof(*(elem));                            \
	__kfree_data_elem_count_size(elem, count, __size);                         \
})

#if __has_feature(address_sanitizer)
# define __kalloc_no_kasan __attribute__((no_sanitize("address")))
#else
# define __kalloc_no_kasan
#endif

#define KALLOC_CONCAT(x, y) __CONCAT(x,y)

#define KALLOC_COUNT_ARGS1(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, N, ...) N
#define KALLOC_COUNT_ARGS(...) \
	KALLOC_COUNT_ARGS1(, ##__VA_ARGS__, _9, _8, _7, _6, _5, _4, _3, _2, _1, _0)
#define KALLOC_DISPATCH1(base, N, ...) __CONCAT(base, N)(__VA_ARGS__)
#define KALLOC_DISPATCH(base, ...) \
	KALLOC_DISPATCH1(base, KALLOC_COUNT_ARGS(__VA_ARGS__), ##__VA_ARGS__)
#define KALLOC_DISPATCH1_R(base, N, ...) __CONCAT(base, N)(__VA_ARGS__)
#define KALLOC_DISPATCH_R(base, ...) \
	KALLOC_DISPATCH1_R(base, KALLOC_COUNT_ARGS(__VA_ARGS__), ##__VA_ARGS__)

#define kt_view_var \
	KALLOC_CONCAT(kalloc_type_view_, __LINE__)

#define KALLOC_TYPE_SEGMENT "__DATA_CONST"

/*
 * When kalloc_type_impl is called from xnu, it calls zalloc_flags
 * directly and doesn't redirect zone-less sites to kheap_alloc.
 * Passing a size larger than KHEAP_MAX_SIZE for these allocations will
 * lead to a panic as the zone is null. Therefore assert that size
 * is less than KALLOC_SAFE_ALLOC_SIZE.
 */
#if XNU_KERNEL_PRIVATE || defined(KALLOC_TYPE_STRICT_SIZE_CHECK)
#define KALLOC_TYPE_SIZE_CHECK(size)                           \
	_Static_assert(size <= KALLOC_SAFE_ALLOC_SIZE,             \
	"type is too large");
#else
#define KALLOC_TYPE_SIZE_CHECK(size)
#endif

#define KALLOC_TYPE_CHECK_2(check, type) \
	(KALLOC_TYPE_SIG_CHECK(check, type))

#define KALLOC_TYPE_CHECK_3(check, type1, type2) \
	(KALLOC_TYPE_SIG_CHECK(check, type1) && \
	    KALLOC_TYPE_SIG_CHECK(check, type2))

#define KALLOC_TYPE_CHECK(...) \
	KALLOC_DISPATCH_R(KALLOC_TYPE_CHECK, ##__VA_ARGS__)

#define KALLOC_TYPE_VM_SIZE_CHECK_1(type) \
	(sizeof(type) > KHEAP_MAX_SIZE)

#define KALLOC_TYPE_VM_SIZE_CHECK_2(type1, type2) \
	(sizeof(type1) + sizeof(type2) > KHEAP_MAX_SIZE)

#define KALLOC_TYPE_VM_SIZE_CHECK(...) \
	KALLOC_DISPATCH_R(KALLOC_TYPE_VM_SIZE_CHECK, ##__VA_ARGS__)

#define KALLOC_TYPE_TRAILING_DATA_CHECK(hdr_ty, elem_ty)     \
	_Static_assert((KALLOC_TYPE_IS_DATA_ONLY(hdr_ty) ||  \
	    !KALLOC_TYPE_IS_DATA_ONLY(elem_ty)),             \
	"cannot allocate data-only array of " #elem_ty       \
	" contiguously to " #hdr_ty)

#ifdef __cplusplus
#define KALLOC_TYPE_CAST_FLAGS(flags) static_cast<kalloc_type_flags_t>(flags)
#else
#define KALLOC_TYPE_CAST_FLAGS(flags) (kalloc_type_flags_t)(flags)
#endif

/*
 * Don't emit signature if type is "data-only" or is large enough that it
 * uses the VM.
 *
 * Note: sig_type is the type you want to emit signature for. The variable
 * args can be used to provide other types in the allocation, to make the
 * decision of whether to emit the signature.
 */
#define KALLOC_TYPE_EMIT_SIG(sig_type, ...)                              \
	(KALLOC_TYPE_CHECK(KT_SUMMARY_MASK_DATA, sig_type, ##__VA_ARGS__) || \
	KALLOC_TYPE_VM_SIZE_CHECK(sig_type, ##__VA_ARGS__))?                 \
	"" : __builtin_xnu_type_signature(sig_type)

/*
 * Kalloc type flags are adjusted to indicate if the type is "data-only" or
 * will use the VM or is a pointer array.
 */
#define KALLOC_TYPE_ADJUST_FLAGS(flags, ...)                                 \
	KALLOC_TYPE_CAST_FLAGS((flags | KT_CHANGED | KT_CHANGED2 |               \
	(KALLOC_TYPE_CHECK(KT_SUMMARY_MASK_DATA, __VA_ARGS__)? KT_DATA_ONLY: 0) |\
	(KALLOC_TYPE_CHECK(KT_SUMMARY_MASK_PTR, __VA_ARGS__)? KT_PTR_ARRAY: 0) | \
	(KALLOC_TYPE_VM_SIZE_CHECK(__VA_ARGS__)? KT_VM : 0)))

#define _KALLOC_TYPE_DEFINE(var, type, flags)                       \
	__kalloc_no_kasan                                               \
	__PLACE_IN_SECTION(KALLOC_TYPE_SEGMENT ", __kalloc_type, "      \
	    "regular, live_support")                                    \
	struct kalloc_type_view var[1] = { {                            \
	    .kt_zv.zv_name = "site." #type,                             \
	    .kt_flags = KALLOC_TYPE_ADJUST_FLAGS(flags, type),          \
	    .kt_size = sizeof(type),                                    \
	    .kt_signature = KALLOC_TYPE_EMIT_SIG(type),                 \
	} };                                                            \
	KALLOC_TYPE_SIZE_CHECK(sizeof(type));

#define KALLOC_TYPE_VAR_DEFINE_3(var, type, flags)                  \
	__kalloc_no_kasan                                               \
	__PLACE_IN_SECTION(KALLOC_TYPE_SEGMENT ", __kalloc_var, "       \
	    "regular, live_support")                                    \
	struct kalloc_type_var_view var[1] = { {                        \
	    .kt_version = KT_V1,                                        \
	    .kt_name = "site." #type,                                   \
	    .kt_flags = KALLOC_TYPE_ADJUST_FLAGS(flags, type),          \
	    .kt_size_type = sizeof(type),                               \
	    .kt_sig_type = KALLOC_TYPE_EMIT_SIG(type),                  \
	} };                                                            \
	KALLOC_TYPE_SIZE_CHECK(sizeof(type));

#define KALLOC_TYPE_VAR_DEFINE_4(var, hdr, type, flags)             \
	__kalloc_no_kasan                                               \
	__PLACE_IN_SECTION(KALLOC_TYPE_SEGMENT ", __kalloc_var, "       \
	    "regular, live_support")                                    \
	struct kalloc_type_var_view var[1] = { {                        \
	    .kt_version = KT_V1,                                        \
	    .kt_name = "site." #hdr "." #type,                          \
	    .kt_flags = KALLOC_TYPE_ADJUST_FLAGS(flags, hdr, type),     \
	    .kt_size_hdr = sizeof(hdr),                                 \
	    .kt_size_type = sizeof(type),                               \
	    .kt_sig_hdr = KALLOC_TYPE_EMIT_SIG(hdr, type),              \
	    .kt_sig_type = KALLOC_TYPE_EMIT_SIG(type, hdr),             \
	} };                                                            \
	KALLOC_TYPE_SIZE_CHECK(sizeof(hdr));                            \
	KALLOC_TYPE_SIZE_CHECK(sizeof(type));                           \
	KALLOC_TYPE_TRAILING_DATA_CHECK(hdr, type);

/*
 * This macro is currently used by AppleImage4
 */
#define KALLOC_TYPE_DEFINE_SITE(var, type, flags)       \
	static _KALLOC_TYPE_DEFINE(var, type, flags)



extern void *__unsafe_indexable kalloc_type_var_impl(
	kalloc_type_var_view_t  kt_view,
	vm_size_t               size,
	zalloc_flags_t          flags,
	void                   *site);

extern void kfree_type_var_impl(
	kalloc_type_var_view_t  kt_view,
	void                   *ptr __unsafe_indexable,
	vm_size_t               size);


__attribute__((malloc, alloc_size(2)))
static inline void *
__sized_by(size)
__kalloc_type_var_impl(
	kalloc_type_var_view_t  kt_view,
	vm_size_t               size,
	zalloc_flags_t          flags,
	void                   *site)
{
	void *__unsafe_indexable addr;

	addr = (kalloc_type_var_impl)(kt_view, size, flags, site);
	if (flags & Z_NOFAIL) {
		__builtin_assume(addr != NULL);
	}
	return __unsafe_forge_bidi_indexable(void *, addr, size);
}

#define kalloc_type_var_impl(ktv, size, fl, site) \
	__kalloc_type_var_impl(ktv, size, fl, site)

extern void *kalloc_type_impl_external(
	kalloc_type_view_t  kt_view,
	zalloc_flags_t      flags);

extern void kfree_type_impl_external(
	kalloc_type_view_t  kt_view,
	void               *ptr __unsafe_indexable);

extern void *OSObject_typed_operator_new(
	kalloc_type_view_t  ktv,
	vm_size_t           size);

extern void OSObject_typed_operator_delete(
	kalloc_type_view_t  ktv,
	void               *mem __unsafe_indexable,
	vm_size_t           size);


extern void kern_os_zfree(
	zone_t        zone,
	void         *addr __unsafe_indexable,
	vm_size_t     size);

__ASSUME_PTR_ABI_SINGLE_END __END_DECLS

#endif  /* _KERN_KALLOC_H_ */

