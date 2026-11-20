/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_NATIVE_SSC_H
#define ELDEN_MOTION_NATIVE_SSC_H
#include "Ssc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void (*server)(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *,int up);
    void (*receive)(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *,const uint8_t *,uint32_t);
} EldenMotionTransportClient;
/* Installed explicitly by the sole shared ADSP/QRTR owner. It subscribes to
 * its existing router, never registers another IPCRTR channel. Callback data
 * are borrowed until return; callbacks can run concurrently with open/close.
 * open/level/send/withdraw are called only by this original sensor worker.
 * Contexts/handles are retained until reset, including failed cleanup. */
typedef struct {
    void *context;uint64_t owner,epoch;uint32_t max_rx_payload;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    void *(*open)(void *,uint64_t owner,uint64_t epoch,uint32_t service,unsigned connection,
                  const EldenMotionTransportClient *,void *client_context);
    int (*level)(void *,void *handle,uint64_t owner,uint64_t epoch,EldenSscServer *);
    int (*send_epoch)(void *,void *handle,uint64_t owner,uint64_t epoch,const EldenSscServer *,const uint8_t *,uint32_t);
    /* 0 only with actual queued-send/callback fencing and remote-inactive
     * proof (or stopped DSP generation); no success on queued DEL_CLIENT. */
    int (*withdraw)(void *,void *handle,uint64_t owner,uint64_t epoch,const EldenSscServer *);
} EldenMotionTransport;
typedef struct {
    void *context;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    int (*qualify)(void *,uint64_t owner,uint64_t epoch,unsigned kind,const EldenSscAttrs *,EldenMotionSpec *);
    int (*map_time)(void *,uint64_t owner,uint64_t epoch,uint64_t ssc_ticks,uint64_t *mach_ticks);
    uint32_t max_age_us,max_future_us;uint8_t ios_negate_axes[2];
} EldenMotionBoard;
typedef struct {
    uint32_t stage;int error,runtime_error;uint32_t hid_started,hid_bound,packets,dropped,stale,samples,terminal;
    uint64_t owner,epoch,generation;
} EldenMotionNativeStatus;
/* Opt-in worker start only. No static initializer, MobileGestalt/rotation/SPU
 * publication, DSP power-up, bank allocation or automatic source substitution. */
int elden_motion_native_ssc_start(const EldenMotionTransport *,const EldenMotionBoard *);
int elden_motion_native_ssc_request_stop(uint64_t owner,uint64_t epoch);
/* Original platform recovery calls stop BEFORE it stops shared ADSP/power.
 * <=5.5s acknowledgement; nonzero retains providers and refuses release. */
int elden_motion_native_ssc_stop(uint64_t owner,uint64_t epoch);
int elden_motion_native_ssc_status(EldenMotionNativeStatus *);
/* Recovery-compatible:1when absent or actually terminal-success,0otherwise. */
int elden_motion_native_ssc_recovery_stop(void);
/* One original-owner, nonblocking status observer installed before start.
 * Calls happen after status lock release on the owning SSC worker; retained
 * until reset. Enables listener/recovery to see a motion fault while next2
 * is waiting, without another polling thread or a fabricated IRQ event. */
int elden_motion_native_ssc_observe(void *context,uint64_t owner,uint64_t epoch,
    void (*status)(void *,const EldenMotionNativeStatus *));
#ifdef __cplusplus
}
#endif
#endif
