/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_CCI_H
#define ELDEN_CAMERA_CCI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenCciAcquire=0,EldenCciTransfer=1,EldenCciCleanup=2};
enum {EldenCciEmpty=0,EldenCciReady=1,EldenCciHeld=2,EldenCciStopped=3};
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Both internal masters/queues of this physical controller are owned.
     * Real CAM power/clocks/CCI pins must remain admitted during every access.
     * No provider defaults, clock guesses, sensor power or CAMSS ownership. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,unsigned controller,unsigned operation);
    int (*read)(void *,uint32_t reg,uint32_t *);
    int (*write)(void *,uint32_t reg,uint32_t);
    uint64_t (*now_us)(void *);
    int (*delay_us)(void *,unsigned);
    /* Owned parent IRQ is kept masked for these bounded polled transactions;
     * actual GIC/handler drain is required before restore/release. */
    int (*mask)(void *,int on);
    int (*drain)(void *);
} EldenCciIo;
typedef union {uint64_t alignment;uint8_t bytes[512];} EldenCci;
typedef struct {uint32_t version,state,transactions,reads,writes,last_status;int error;} EldenCciStatus;
/* Actual source-configured37.5MHz/400kHz v2 controller, internal master0.
 * controller0=rear CCI0;controller1=front CCI1, NOT internal master1.
 * Retain partial state on failure; init/transaction refuse a held session. */
int elden_camera_cci_init(EldenCci *,const EldenCciIo *,unsigned controller,uint32_t clock_hz);
int elden_camera_cci_write(EldenCci *,uint8_t slave,const uint8_t *,size_t);
int elden_camera_cci_read(EldenCci *,uint8_t slave,uint8_t *,size_t);
/* Register helpers serialize the address write+read as one caller operation.
 * Original controller uses separate queue0 WRITE then queue1 READ commands;
 * no guessed lock/disable-stop/retry opcode. Atomic output on read failure. */
int elden_camera_cci_write_register(EldenCci *,uint8_t slave,uint16_t reg,unsigned width,uint32_t value);
int elden_camera_cci_read_register(EldenCci *,uint8_t slave,uint16_t reg,unsigned width,uint32_t *);
/* Actual HALT/master0 reset acknowledgment, queues/read buffers idle, IRQ drain
 * and original timing/mask restoration. Failure retains maps/power/owner. */
int elden_camera_cci_stop(EldenCci *);
int elden_camera_cci_status(EldenCci *,EldenCciStatus *);
#ifdef __cplusplus
}
#endif
#endif
