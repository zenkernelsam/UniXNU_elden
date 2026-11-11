/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Wcn7861Peripheral.h"
#include "BtH4.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;
    int (*write)(void *,const uint8_t *,size_t,unsigned timeout_ms);
    size_t (*read)(void *,uint8_t *,size_t);
    uint64_t (*now_ms)(void *);
    void (*delay_ms)(void *,unsigned);
    int (*rfr)(void *,bool ready);
    int (*baud)(void *,uint32_t);
    void (*report)(void *,unsigned stage,int error);
} EldenWcnStartupIo;
typedef struct {
    EldenWcnStartupIo io;PbH4Rx h4;
    uint8_t receive[512];size_t received,position;
    uint8_t packet[ELDEN_WCN_PERIPHERAL_PACKET];
    EldenWcnPeripheralPlan firmware[3];
    EldenWcnPeripheralReply identity;
    uint32_t baud,notifications,expected_notification;
    unsigned stage,frames,ignored,writes;
    int error;bool poisoned,ready,wake_ack,hci_owned,sleeping;
} EldenWcnStartup;
enum {EldenWcnStartupArgument=-22,EldenWcnStartupProtocol=-71,EldenWcnStartupTimeout=-110,EldenWcnStartupIdentity=-19};
/* Full immutable firmware preflight happens before any wire operation.
 * Caller verifies board SHA manifest and holds UART/radio power ownership.
 * Failure poisons this instance: only a qualified physical reset makes a new
 * instance safe. No retry, flash write or native capability fabrication.
 */
int elden_wcn_startup_prepare(EldenWcnStartup *,const EldenWcnStartupIo *,
    const uint8_t *patch,size_t patch_bytes,const uint8_t *nvm,size_t nvm_bytes,
    const uint8_t *tmel,size_t tmel_bytes);
int elden_wcn_startup_run(EldenWcnStartup *);
#ifdef __cplusplus
}
#endif
