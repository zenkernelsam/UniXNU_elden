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
 *	File:	vm/pmap.h
 *	Author:	Avadis Tevanian, Jr.
 *	Date:	1985
 *
 *	Machine address mapping definitions -- machine-independent
 *	section.  [For machine-dependent section, see "machine/pmap.h".]
 */

#ifndef _VM_PMAP_H_
#define _VM_PMAP_H_

#include <mach/kern_return.h>
#include <mach/vm_param.h>
#include <mach/vm_types.h>
#include <mach/vm_attributes.h>
#include <mach/boolean.h>
#include <mach/vm_prot.h>
#include <kern/trustcache.h>

#if __has_include(<CoreEntitlements/CoreEntitlements.h>)
#include <CoreEntitlements/CoreEntitlements.h>
#endif


/*
 *	The following is a description of the interface to the
 *	machine-dependent "physical map" data structure.  The module
 *	must provide a "pmap_t" data type that represents the
 *	set of valid virtual-to-physical addresses for one user
 *	address space.  [The kernel address space is represented
 *	by a distinguished "pmap_t".]  The routines described manage
 *	this type, install and update virtual-to-physical mappings,
 *	and perform operations on physical addresses common to
 *	many address spaces.
 */

/* Copy between a physical page and a virtual address */
/* LP64todo - switch to vm_map_offset_t when it grows */
extern kern_return_t    copypv(
	addr64_t source,
	addr64_t sink,
	unsigned int size,
	int which);
#define cppvPsnk        1
#define cppvPsnkb      31
#define cppvPsrc        2
#define cppvPsrcb      30
#define cppvFsnk        4
#define cppvFsnkb      29
#define cppvFsrc        8
#define cppvFsrcb      28
#define cppvNoModSnk   16
#define cppvNoModSnkb  27
#define cppvNoRefSrc   32
#define cppvNoRefSrcb  26
#define cppvKmap       64       /* Use the kernel's vm_map */
#define cppvKmapb      25

extern boolean_t pmap_has_managed_page(ppnum_t first, ppnum_t last);



extern boolean_t        pmap_is_noencrypt(ppnum_t);
extern void             pmap_set_noencrypt(ppnum_t pn);
extern void             pmap_clear_noencrypt(ppnum_t pn);

/*
 * JMM - This portion is exported to other kernel components right now,
 * but will be pulled back in the future when the needed functionality
 * is provided in a cleaner manner.
 */

extern const pmap_t     kernel_pmap;            /* The kernel's map */
#define pmap_kernel()   (kernel_pmap)

#define VM_MEM_SUPERPAGE        0x100           /* map a superpage instead of a base page */
#define VM_MEM_STACK            0x200

/* N.B. These use the same numerical space as the PMAP_EXPAND_OPTIONS
 * definitions in i386/pmap_internal.h
 */
#define PMAP_CREATE_64BIT          0x1

#if __x86_64__

#define PMAP_CREATE_EPT            0x2
#define PMAP_CREATE_TEST           0x4 /* pmap will be used for testing purposes only */
#define PMAP_CREATE_KNOWN_FLAGS (PMAP_CREATE_64BIT | PMAP_CREATE_EPT | PMAP_CREATE_TEST)

#else

#define PMAP_CREATE_STAGE2         0
#if __arm64e__
#define PMAP_CREATE_DISABLE_JOP    0x4
#else
#define PMAP_CREATE_DISABLE_JOP    0
#endif
#if __ARM_MIXED_PAGE_SIZE__
#define PMAP_CREATE_FORCE_4K_PAGES 0x8
#else
#define PMAP_CREATE_FORCE_4K_PAGES 0
#endif /* __ARM_MIXED_PAGE_SIZE__ */
#define PMAP_CREATE_X86_64         0
#if CONFIG_ROSETTA
#define PMAP_CREATE_ROSETTA        0x20
#else
#define PMAP_CREATE_ROSETTA        0
#endif /* CONFIG_ROSETTA */

#define PMAP_CREATE_TEST           0x40 /* pmap will be used for testing purposes only */

/* Define PMAP_CREATE_KNOWN_FLAGS in terms of optional flags */
#define PMAP_CREATE_KNOWN_FLAGS (PMAP_CREATE_64BIT | PMAP_CREATE_STAGE2 | PMAP_CREATE_DISABLE_JOP | \
    PMAP_CREATE_FORCE_4K_PAGES | PMAP_CREATE_X86_64 | PMAP_CREATE_ROSETTA | PMAP_CREATE_TEST)

#endif /* __x86_64__ */

#define PMAP_OPTIONS_NOWAIT     0x1             /* don't block, return
	                                         * KERN_RESOURCE_SHORTAGE
	                                         * instead */
#define PMAP_OPTIONS_NOENTER    0x2             /* expand pmap if needed
	                                         * but don't enter mapping
	                                         */
#define PMAP_OPTIONS_COMPRESSOR 0x4             /* credit the compressor for
	                                         * this operation */
#define PMAP_OPTIONS_INTERNAL   0x8             /* page from internal object */
#define PMAP_OPTIONS_REUSABLE   0x10            /* page is "reusable" */
#define PMAP_OPTIONS_NOFLUSH    0x20            /* delay flushing of pmap */
#define PMAP_OPTIONS_NOREFMOD   0x40            /* don't need ref/mod on disconnect */
#define PMAP_OPTIONS_ALT_ACCT   0x80            /* use alternate accounting scheme for page */
#define PMAP_OPTIONS_REMOVE     0x100           /* removing a mapping */
#define PMAP_OPTIONS_SET_REUSABLE   0x200       /* page is now "reusable" */
#define PMAP_OPTIONS_CLEAR_REUSABLE 0x400       /* page no longer "reusable" */
#define PMAP_OPTIONS_COMPRESSOR_IFF_MODIFIED 0x800 /* credit the compressor
	                                            * iff page was modified */
#define PMAP_OPTIONS_PROTECT_IMMEDIATE 0x1000   /* allow protections to be
	                                         * be upgraded */
#define PMAP_OPTIONS_CLEAR_WRITE 0x2000
#define PMAP_OPTIONS_TRANSLATED_ALLOW_EXECUTE 0x4000 /* Honor execute for translated processes */
#if defined(__arm__) || defined(__arm64__)
#define PMAP_OPTIONS_FF_LOCKED  0x8000
#define PMAP_OPTIONS_FF_WIRED   0x10000
#endif
#define PMAP_OPTIONS_XNU_USER_DEBUG 0x20000

/* Indicates that pmap_enter() or pmap_remove() is being called with preemption already disabled. */
#define PMAP_OPTIONS_NOPREEMPT  0x80000

#define PMAP_OPTIONS_MAP_TPRO 0x40000

#define PMAP_OPTIONS_RESERVED_MASK 0xFF000000   /* encoding space reserved for internal pmap use */

#if     !defined(__LP64__)
extern vm_offset_t      pmap_extract(pmap_t pmap,
    vm_map_offset_t va);
#endif
extern void             pmap_change_wiring(     /* Specify pageability */
	pmap_t          pmap,
	vm_map_offset_t va,
	boolean_t       wired);

/* LP64todo - switch to vm_map_offset_t when it grows */
extern void             pmap_remove(    /* Remove mappings. */
	pmap_t          map,
	vm_map_offset_t s,
	vm_map_offset_t e);

extern void             pmap_remove_options(    /* Remove mappings. */
	pmap_t          map,
	vm_map_offset_t s,
	vm_map_offset_t e,
	int             options);

extern void             fillPage(ppnum_t pa, unsigned int fill);

#if defined(__LP64__)
extern void pmap_pre_expand(pmap_t pmap, vm_map_offset_t vaddr);
extern kern_return_t pmap_pre_expand_large(pmap_t pmap, vm_map_offset_t vaddr);
extern vm_size_t pmap_query_pagesize(pmap_t map, vm_map_offset_t vaddr);
#endif

mach_vm_size_t pmap_query_resident(pmap_t pmap,
    vm_map_offset_t s,
    vm_map_offset_t e,
    mach_vm_size_t *compressed_bytes_p);

extern void pmap_set_vm_map_cs_enforced(pmap_t pmap, bool new_value);
extern bool pmap_get_vm_map_cs_enforced(pmap_t pmap);

/* Inform the pmap layer that there is a JIT entry in this map. */
extern void pmap_set_jit_entitled(pmap_t pmap);

/* Ask the pmap layer if there is a JIT entry in this map. */
extern bool pmap_get_jit_entitled(pmap_t pmap);

/* Inform the pmap layer that the XO register is repurposed for this map */
extern void pmap_set_tpro(pmap_t pmap);

/* Ask the pmap layer if there is a TPRO entry in this map. */
extern bool pmap_get_tpro(pmap_t pmap);

/*
 * Tell the pmap layer what range within the nested region the VM intends to
 * use.
 */
extern void pmap_trim(pmap_t grand, pmap_t subord, addr64_t vstart, uint64_t size);

extern bool pmap_is_nested(pmap_t pmap);

/*
 * Dump page table contents into the specified buffer.  Returns KERN_INSUFFICIENT_BUFFER_SIZE
 * if insufficient space, KERN_NOT_SUPPORTED if unsupported in the current configuration.
 * This is expected to only be called from kernel debugger context,
 * so synchronization is not required.
 */

extern kern_return_t pmap_dump_page_tables(pmap_t pmap, void *bufp, void *buf_end, unsigned int level_mask, size_t *bytes_copied);

/* Asks the pmap layer for number of bits used for VA address. */
extern uint32_t pmap_user_va_bits(pmap_t pmap);
extern uint32_t pmap_kernel_va_bits(void);

/*
 * Indicates if any special policy is applied to this protection by the pmap
 * layer.
 */
bool pmap_has_prot_policy(pmap_t pmap, bool translated_allow_execute, vm_prot_t prot);

/*
 * Causes the pmap to return any available pages that it can return cheaply to
 * the VM.
 */
uint64_t pmap_release_pages_fast(void);

#define PMAP_QUERY_PAGE_PRESENT                 0x01
#define PMAP_QUERY_PAGE_REUSABLE                0x02
#define PMAP_QUERY_PAGE_INTERNAL                0x04
#define PMAP_QUERY_PAGE_ALTACCT                 0x08
#define PMAP_QUERY_PAGE_COMPRESSED              0x10
#define PMAP_QUERY_PAGE_COMPRESSED_ALTACCT      0x20
extern kern_return_t pmap_query_page_info(
	pmap_t          pmap,
	vm_map_offset_t va,
	int             *disp);

extern bool pmap_in_ppl(void);

extern uint32_t pmap_lookup_in_static_trust_cache(const uint8_t cdhash[CS_CDHASH_LEN]);
extern bool pmap_lookup_in_loaded_trust_caches(const uint8_t cdhash[CS_CDHASH_LEN]);

/**
 * Indicates whether the device supports register-level MMIO access control.
 *
 * @note Unlike the pmap-io-ranges mechanism, which enforces PPL-only register
 *       writability at page granularity, this mechanism allows specific registers
 *       on a read-mostly page to be written using a dedicated guarded mode trap
 *       without requiring a full PPL driver extension.
 *
 * @return True if the device supports register-level MMIO access control.
 */
extern bool pmap_has_iofilter_protected_write(void);

/**
 * Performs a write to the I/O register specified by addr on supported devices.
 *
 * @note On supported devices (determined by pmap_has_iofilter_protected_write()), this
 *       function goes over the sorted I/O filter entry table. If there is a hit, the
 *       write is performed from Guarded Mode. Otherwise, the write is performed from
 *       Normal Mode (kernel mode). Note that you can still hit an exception if the
 *       register is owned by PPL but not allowed by an io-filter-entry in the device tree.
 *
 * @note On unsupported devices, this function will panic.
 *
 * @param addr The address of the register.
 * @param value The value to be written.
 * @param width The width of the I/O register, supported values are 1, 2, 4 and 8.
 */
extern void pmap_iofilter_protected_write(vm_address_t addr, uint64_t value, uint64_t width);

extern void *pmap_claim_reserved_ppl_page(void);
extern void pmap_free_reserved_ppl_page(void *kva);

extern void pmap_ledger_verify_size(size_t);
extern ledger_t pmap_ledger_alloc(void);
extern void pmap_ledger_free(ledger_t);

extern bool pmap_is_bad_ram(ppnum_t ppn);

#if __arm64__
extern bool pmap_is_exotic(pmap_t pmap);
#else /* __arm64__ */
#define pmap_is_exotic(pmap) false
#endif /* __arm64__ */


/*
 * Returns a subset of pmap_cs non-default configuration,
 * e.g. loosening up of some restrictions through pmap_cs or amfi
 * boot-args. The return value is a bit field with possible bits
 * described below. If default, the function will return 0. Note that
 * this does not work the other way: 0 does not imply that pmap_cs
 * runs in default configuration, and only a small configuration
 * subset is returned by this function.
 *
 * Never assume the system is "secure" if this returns 0.
 */
extern int pmap_cs_configuration(void);


#if CONFIG_SPTM
/*
 * The TrustedExecutionMonitor address space data structure is kept within the
 * pmap structure in order to provide a coherent API to the rest of the kernel
 * for working with code signing monitors.
 *
 * However, a lot of parts of the kernel don't have visibility into the pmap
 * data structure as they are opaque unless you're in the Mach portion of the
 * kernel. To allievate this, we provide pmap APIs to the rest of the kernel.
 */
#include <TrustedExecutionMonitor/API.h>

/*
 * All pages allocated by TXM are also kept within the TXM VM object, which allows
 * tracking it for accounting and debugging purposes.
 */
extern vm_object_t txm_vm_object;

/**
 * Acquire the pointer of the kernel pmap being used for the system.
 */
extern pmap_t
pmap_txm_kernel_pmap(void);

/**
 * Acquire the TXM address space object stored within the pmap.
 */
extern TXMAddressSpace_t*
pmap_txm_addr_space(const pmap_t pmap);

/**
 * Set the TXM address space object within the pmap.
 */
extern void
pmap_txm_set_addr_space(
	pmap_t pmap,
	TXMAddressSpace_t *txm_addr_space);

/**
 * Set the trust level of the TXM address space object within the pmap.
 */
extern void
pmap_txm_set_trust_level(
	pmap_t pmap,
	CSTrust_t trust_level);

/**
 * Get the trust level of the TXM address space object within the pmap.
 */
extern kern_return_t
pmap_txm_get_trust_level_kdp(
	pmap_t pmap,
	CSTrust_t *trust_level);

/**
 * Take a shared lock on the pmap in order to enforce safe concurrency for
 * an operation on the TXM address space object. Passing in NULL takes the lock
 * on the current pmap.
 */
extern void
pmap_txm_acquire_shared_lock(pmap_t pmap);

/**
 * Release the shared lock which was previously acquired for operations on
 * the TXM address space object. Passing in NULL releases the lock for the
 * current pmap.
 */
extern void
pmap_txm_release_shared_lock(pmap_t pmap);

/**
 * Take an exclusive lock on the pmap in order to enforce safe concurrency for
 * an operation on the TXM address space object. Passing in NULL takes the lock
 * on the current pmap.
 */
extern void
pmap_txm_acquire_exclusive_lock(pmap_t pmap);

/**
 * Release the exclusive lock which was previously acquired for operations on
 * the TXM address space object. Passing in NULL releases the lock for the
 * current pmap.
 */
extern void
pmap_txm_release_exclusive_lock(pmap_t pmap);

/**
 * Transfer a page to the TXM_DEFAULT type after resolving its mapping from its
 * virtual to physical address.
 */
extern void
pmap_txm_transfer_page(const vm_address_t addr);

/**
 * Grab an available page from the VM free list, add it to the TXM VM object and
 * then transfer it to be owned by TXM.
 *
 * Returns the physical address of the page allocated.
 */
extern vm_map_address_t
pmap_txm_allocate_page(void);

#endif /* CONFIG_SPTM */



#endif  /* _VM_PMAP_H_ */
