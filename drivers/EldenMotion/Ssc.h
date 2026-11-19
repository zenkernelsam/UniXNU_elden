/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_SSC_H
#define ELDEN_MOTION_SSC_H
#include "Core.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {EldenSscService=400,EldenSscConnections=3,EldenSscMaxPacket=1024,
      EldenSscMaxRates=16,EldenSscMaxRanges=4};
enum {EldenSscOff=0,EldenSscDiscovering=1,EldenSscDescribing=2,
      EldenSscReady=3,EldenSscStopping=4,EldenSscStopped=5,EldenSscHeld=6};
enum {EldenSscAgain=-11,EldenSscAbsent=-19,EldenSscBusy=-16,
      EldenSscClosed=-125,EldenSscOverflow=-75};
typedef struct {
    char name[32],vendor[32],type[32],api[48];
    uint32_t seen,rates[EldenSscMaxRates],rate_count;
    uint32_t ranges[EldenSscMaxRanges][2],range_count;
    int64_t hw_id;uint8_t available,physical,stream_type;
} EldenSscAttrs;
typedef struct {
    uint32_t service,instance,node,port;uint64_t generation;
} EldenSscServer;
/* The actual shared ADSP/QRTR owner supplies these operations. No competing
 * IPCRTR channel, sensor-PD boot, SMMU map or fake firmware is created here.
 * One connection for discovery, one each for accel/gyro, as original libssc.
 * Send=0 means queued, never a sample/firmware acknowledgement. Withdrawal=0
 * must prove the original port is closed, queued sends fenced and its remote
 * sensor client inactive (or that the old DSP generation has really stopped).
 * Merely queuing QRTR DEL_CLIENT is not this proof. */
typedef struct {
    void *context;uint64_t owner,epoch;
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    int (*send)(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *,
                unsigned connection,const uint8_t *,uint32_t);
    int (*withdraw)(void *,uint64_t owner,uint64_t epoch,const EldenSscServer *,unsigned connection);
    /* Qualify actual OEM API/units/range and physical mount. Neither the
     * donor's mounting nor a mathematically valid permutation is evidence. */
    int (*qualify)(void *,uint64_t owner,uint64_t epoch,unsigned kind,
                   const EldenSscAttrs *,EldenMotionSpec *);
    /* Explicit live clock synchronization; no restamping or identity fallback. */
    int (*map_time)(void *,uint64_t owner,uint64_t epoch,uint64_t ssc_ticks,uint64_t *mach_ticks);
    int (*deliver)(void *,uint64_t owner,uint64_t epoch,const EldenMotionSample *);
} EldenSscOps;
typedef struct {
    uint16_t next_transaction,pending_transaction;
    uint32_t pending_message,pending_kind,pending_rate;
    uint64_t sent_ticks,client,early_ticks;uint8_t pending,used,withdrawn;
    uint32_t early_length;uint8_t early[EldenSscMaxPacket];
} EldenSscConnection;
typedef struct {
    EldenMotionSuid suid;EldenMotionSpec spec;EldenSscAttrs attrs;
    uint32_t demand_hz,active_rate;uint64_t last_ssc,last_mach,attribute_ticks;
    uint8_t discovered,described,qualified,lookup_accepted,attribute_accepted;
    uint8_t attribute_seen,disable_confirmed;
} EldenSscSensor;
typedef struct {
    EldenSscOps ops;EldenSscServer server;EldenSscConnection conn[EldenSscConnections];
    EldenSscSensor sensor[2];uint64_t last_ticks,stop_deadline,retry_ticks;
    uint32_t state,requests,responses,reports,samples,rejected;
    int error;uint8_t initialized,server_up,has_clock,closing;
} EldenSsc;

/* All manager methods run on the owning sensor worker. Caller tags every
 * queued callback with the actual transport owner/epoch and server generation. */
int elden_ssc_init(EldenSsc *,const EldenSscOps *);
int elden_ssc_server(EldenSsc *,uint64_t owner,uint64_t epoch,const EldenSscServer *,int up,uint64_t now_ticks);
int elden_ssc_step(EldenSsc *,uint64_t now_ticks);
int elden_ssc_receive(EldenSsc *,uint64_t owner,uint64_t epoch,const EldenSscServer *,unsigned connection,
                      const uint8_t *,size_t,uint64_t now_ticks);
int elden_ssc_demand(EldenSsc *,uint64_t owner,uint64_t epoch,unsigned kind,uint32_t hz);
int elden_ssc_stop(EldenSsc *,uint64_t owner,uint64_t epoch,uint64_t now_ticks);
/* Read-only readiness for native HID; includes actual firmware attributes,
 * live session and provider proof. Bind event hooks before starting demands. */
int elden_ssc_admit(EldenSsc *,uint64_t owner,uint64_t epoch,unsigned kind,int cleanup);
uint32_t elden_ssc_idle_ms(const EldenSsc *,uint64_t now_ticks);

/* Source-bound strict codec. Failure preserves output. Reports are validated
 * as a whole before a single attribute, sample or callback can be committed. */
size_t elden_ssc_attribute_request(uint8_t *,size_t,uint16_t,EldenMotionSuid);
size_t elden_ssc_discovery_request(uint8_t *,size_t,uint16_t,unsigned kind);
size_t elden_ssc_sampling_request(uint8_t *,size_t,uint16_t,EldenMotionSuid,uint32_t rate_bits,int disable);
int elden_ssc_attribute_report(const uint8_t *,size_t,uint64_t client,EldenMotionSuid,unsigned kind,EldenSscAttrs *);
int elden_ssc_report_error(const uint8_t *,size_t,uint64_t client,EldenMotionSuid,int64_t *);
int elden_ssc_packet_identity(const uint8_t *,size_t,uint8_t *type,uint16_t *transaction);
uint32_t elden_ssc_pick_rate(const EldenSscAttrs *,uint32_t hz);
int elden_ssc_spec_valid(const EldenMotionSpec *,const EldenSscAttrs *,unsigned kind,EldenMotionSuid);

#ifdef __cplusplus
}
#endif
#endif
