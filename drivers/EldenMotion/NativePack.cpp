/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "NativePack.h"
#include "RpcPack.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/crypto/sha2.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
extern "C" boolean_t ml_at_interrupt_context(void);
extern volatile int native_owner_data_status;
#include "NativePackMethods.inc"
