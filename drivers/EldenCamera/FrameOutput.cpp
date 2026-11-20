/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "FrameOutput.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/task.h>
extern "C" {
boolean_t ml_at_interrupt_context(void);
void *elden_camera_output_bytes(IOBufferMemoryDescriptor *) __asm__("__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv");
IOReturn elden_camera_output_prepare(IOGeneralMemoryDescriptor *,IOOptionBits) __asm__("__ZN25IOGeneralMemoryDescriptor7prepareEj");
}
#include "FrameOutputMethods.inc"
