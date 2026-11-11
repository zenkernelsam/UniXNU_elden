/*
 * Copyright (c) 2000-2018 Apple Inc. All rights reserved.
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
 *
 */
#ifndef _MACH_VM_TYPES_H_
#define _MACH_VM_TYPES_H_

#include <mach/port.h>
#include <mach/machine/vm_types.h>

#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

typedef vm_offset_t             pointer_t __kernel_ptr_semantics;
typedef vm_offset_t             vm_address_t __kernel_ptr_semantics;

/*
 * We use addr64_t for 64-bit addresses that are used on both
 * 32 and 64-bit machines.  On PPC, they are passed and returned as
 * two adjacent 32-bit GPRs.  We use addr64_t in places where
 * common code must be useable both on 32 and 64-bit machines.
 */
typedef uint64_t addr64_t;              /* Basic effective address */

/*
 * We use reg64_t for addresses that are 32 bits on a 32-bit
 * machine, and 64 bits on a 64-bit machine, but are always
 * passed and returned in a single GPR on PPC.  This type
 * cannot be used in generic 32-bit c, since on a 64-bit
 * machine the upper half of the register will be ignored
 * by the c compiler in 32-bit mode.  In c, we can only use the
 * type in prototypes of functions that are written in and called
 * from assembly language.  This type is basically a comment.
 */
typedef uint32_t        reg64_t;

/*
 * To minimize the use of 64-bit fields, we keep some physical
 * addresses (that are page aligned) as 32-bit page numbers.
 * This limits the physical address space to 16TB of RAM.
 */
typedef uint32_t ppnum_t __kernel_ptr_semantics; /* Physical page number */
#define PPNUM_MAX UINT32_MAX


__options_decl(vm_map_create_options_t, uint32_t, {
	VM_MAP_CREATE_DEFAULT          = 0x00000000,
	VM_MAP_CREATE_PAGEABLE         = 0x00000001,
	VM_MAP_CREATE_CORPSE_FOOTPRINT = 0x00000002,
	VM_MAP_CREATE_DISABLE_HOLELIST = 0x00000004,
	VM_MAP_CREATE_NEVER_FAULTS     = 0x00000008,
});

/*
 * Use specifically typed null structures for these in
 * other parts of the kernel to enable compiler warnings
 * about type mismatches, etc...  Otherwise, these would
 * be void*.
 */

typedef struct pmap             *pmap_t;
typedef struct _vm_map          *vm_map_t, *vm_map_read_t, *vm_map_inspect_t;
typedef struct vm_object        *vm_object_t;
typedef struct vm_object_fault_info     *vm_object_fault_info_t;
typedef struct upl              *upl_t;
typedef struct vm_map_copy      *vm_map_copy_t;
typedef struct vm_named_entry   *vm_named_entry_t;

#define PMAP_NULL               ((pmap_t) NULL)
#define VM_OBJECT_NULL          ((vm_object_t) NULL)
#define VM_MAP_COPY_NULL        ((vm_map_copy_t) NULL)


#define VM_MAP_NULL             ((vm_map_t) NULL)
#define VM_MAP_INSPECT_NULL     ((vm_map_inspect_t) NULL)
#define VM_MAP_READ_NULL        ((vm_map_read_t) NULL)
#define UPL_NULL                ((upl_t) NULL)
#define VM_NAMED_ENTRY_NULL     ((vm_named_entry_t) NULL)

/*
 * Evolving definitions, likely to change.
 */

typedef uint64_t                vm_object_offset_t;
typedef uint64_t                vm_object_size_t;

/*!
 * @typedef mach_vm_range_t
 *
 * @brief
 * Pair of a min/max address used to denote a memory region.
 *
 * @discussion
 * @c min_address must be smaller or equal to @c max_address.
 */
typedef struct mach_vm_range {
	mach_vm_offset_t        min_address;
	mach_vm_offset_t        max_address;
} *mach_vm_range_t;

/*!
 * @enum mach_vm_range_flavor_t
 *
 * @brief
 * A flavor for the mach_vm_range_create() call.
 *
 * @const MACH_VM_RANGE_FLAVOR_V1
 * The recipe is an array of @c mach_vm_range_recipe_v1_t.
 */
__enum_decl(mach_vm_range_flavor_t, uint32_t, {
	MACH_VM_RANGE_FLAVOR_INVALID,
	MACH_VM_RANGE_FLAVOR_V1,
});


/*!
 * @enum mach_vm_range_flags_t
 *
 * @brief
 * Flags used to alter the behavior of a Mach VM Range.
 */
__options_decl(mach_vm_range_flags_t, uint64_t, {
	MACH_VM_RANGE_NONE      = 0x000000000000,
});


/*!
 * @enum mach_vm_range_tag_t
 *
 * @brief
 * A tag to denote the semantics of a given Mach VM Range.
 *
 * @const MACH_VM_RANGE_DEFAULT
 * The tag associated with the general VA space usable
 * before the shared cache.
 * Such a range can't be made by userspace.
 *
 * @const MACH_VM_RANGE_DATA
 * The tag associated with the anonymous randomly slid
 * range of data heap optionally made when a process is created.
 * Such a range can't be made by userspace.
 *
 * @const MACH_VM_RANGE_FIXED
 * The tag associated with ranges that are made available
 * for @c VM_FLAGS_FIXED allocations, but that the VM will never
 * autonomously serve from a @c VM_FLAGS_ANYWHERE kind of request.
 * This really create a delegated piece of VA that can be carved out
 * in the way userspace sees fit.
 */
__enum_decl(mach_vm_range_tag_t, uint16_t, {
	MACH_VM_RANGE_DEFAULT,
	MACH_VM_RANGE_DATA,
	MACH_VM_RANGE_FIXED,
});

#pragma pack(1)

typedef struct {
	mach_vm_range_flags_t   flags: 48;
	mach_vm_range_tag_t     range_tag  : 8;
	uint8_t                 vm_tag : 8;
	struct mach_vm_range    range;
} mach_vm_range_recipe_v1_t;

#pragma pack()

#define MACH_VM_RANGE_FLAVOR_DEFAULT MACH_VM_RANGE_FLAVOR_V1
typedef mach_vm_range_recipe_v1_t    mach_vm_range_recipe_t;

typedef uint8_t                *mach_vm_range_recipes_raw_t;


typedef struct {
	uint64_t rtfabstime; // mach_continuous_time at start of fault
	uint64_t rtfduration; // fault service duration
	uint64_t rtfaddr; // fault address
	uint64_t rtfpc; // userspace program counter of thread incurring the fault
	uint64_t rtftid; // thread ID
	uint64_t rtfupid; // process identifier
	uint64_t rtftype; // fault type
} vm_rtfault_record_t;


__END_DECLS

#endif  /* _MACH_VM_TYPES_H_ */
