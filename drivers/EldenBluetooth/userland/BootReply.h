/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "BootstrapStatus.h"
#include <stdbool.h>
typedef struct {int error;bool identity;} EldenBtBootReply;
EldenBtBootReply elden_bt_boot_reply(const char *message,const char *script,const char *requested_address,
    const EldenBtBootStatus *status,size_t bytes,int read_error);
