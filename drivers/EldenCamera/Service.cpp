/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* OS17 service inheritance/manual late metaclasses follows qualified Elden
 * haptics and T970 camera donors. Hardware/frame producer remains original. */
#include "Service.h"
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <mach/kmod.h>
#include <libkern/c++/OSMetaClass.h>
extern kmod_info_t g_kernel_kmod_info;
extern "C" {
void elden_camera_service_static_init(void) __asm__("__GLOBAL__sub_I_Service.cpp");
boolean_t ml_at_interrupt_context(void);
}
#include "ServiceMethods.inc"
