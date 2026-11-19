/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Uses the exact OS17 qualified IOHIDDevice and original event constructors.
 * No MMIO/QRTR/FastRPC/firmware/device-orientation service is created here. */
#include "OriginalHidDeviceABI.h"
#include "MotionOriginalAbi.h"
#include "NativeHid.h"
#include "Hid.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSUnserialize.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
extern kmod_info_t g_kernel_kmod_info;
class IOCatalogue;
extern IOCatalogue *gIOCatalogue;
extern "C" {
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void elden_motion_hid_static_init(void) __asm__("__GLOBAL__sub_I_EldenMotionHid.cpp");
boolean_t ml_at_interrupt_context(void);
bool elden_motion_catalogue_add(IOCatalogue *,OSArray *,bool) __asm__("__ZN11IOCatalogue10addDriversEP7OSArrayb");
}
#include "NativeHidMethods.inc"
