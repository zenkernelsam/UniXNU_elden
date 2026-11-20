/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativeDma.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/task.h>
extern "C" boolean_t ml_at_interrupt_context(void);
extern "C" {
void *elden_camera_dma_bytes(IOBufferMemoryDescriptor *) __asm__("__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv");
IOReturn elden_camera_dma_prepare(IOGeneralMemoryDescriptor *,IOOptionBits) __asm__("__ZN25IOGeneralMemoryDescriptor7prepareEj");
uint64_t elden_camera_dma_segment(IOGeneralMemoryDescriptor *,IOByteCount,IOByteCount *,IOOptionBits) __asm__("__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEyPyj");
uintptr_t elden_camera_dma_virtual(IOMemoryMap *) __asm__("__ZN11IOMemoryMap17getVirtualAddressEv");
}
namespace {
#include "DmaArchitecture.inc"
}
#include "NativeDmaMethods.inc"
