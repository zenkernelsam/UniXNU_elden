/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_MOTION_QRTR_PORTS_H
#define ELDEN_MOTION_QRTR_PORTS_H
#include "AdspChannel.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {EldenQrtrPorts=12,EldenQrtrRemoteServers=32,EldenQrtrMaxData=480,
      EldenQrtrMaxReceive=1024};
#define ELDEN_QRTR_ANY_INSTANCE UINT32_MAX
typedef struct {uint32_t service,instance,node,port;uint64_t generation;} EldenQrtrServer;
typedef struct EldenQrtrPort EldenQrtrPort;
typedef struct {
    void (*server)(void *,uint64_t owner,uint64_t epoch,const EldenQrtrServer *,int up);
    void (*receive)(void *,uint64_t owner,uint64_t epoch,const EldenQrtrServer *,const uint8_t *,uint32_t);
} EldenQrtrClient;
typedef struct {
    uint32_t number,channel_epoch,closing,terminal,callbacks,inflight,del_client_queued;
    uint64_t owner,epoch,requests,received;EldenQrtrServer last_server;int error;
} EldenQrtrPortStatus;
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Real original-owner reviewed IPCRTR receive intents. Configuration is
     * accepted only BEFORE the existing router starts; never donor defaults. */
    uint32_t rx_intent_size,rx_intent_count;
    void (*lock)(void *);void (*unlock)(void *);
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    /* Called only after local in-flight/callback and observed original ADSP
     * queue/pump fences. DEL_CLIENT queued is metadata, NOT a remote ACK.
     * 0 requires actual sensor-client inactivity or stopped old DSP generation. */
    int (*retire)(void *,const EldenQrtrPortStatus *);
} EldenQrtrPortOps;

int elden_qrtr_ports_prepare(const EldenQrtrPortOps *);
int elden_qrtr_ports_owner(uint64_t *owner,uint64_t *epoch,uint32_t *max_receive);
int elden_qrtr_ports_admit(uint64_t owner,uint64_t epoch,int cleanup);
/* These producer hooks run only on the existing ShengQrtr DSP executor. */
int elden_qrtr_ports_rx_config(uint32_t *size,uint32_t *count);
int elden_qrtr_ports_bind(ShengAdspChannel *,uint32_t rx_intent_size);
void elden_qrtr_ports_link(uint32_t channel_epoch,int up);
void elden_qrtr_ports_snapshot(uint32_t channel_epoch,int hello,uint32_t remote_node,const EldenQrtrServer *,uint32_t count);
void elden_qrtr_ports_receive(uint32_t channel_epoch,uint32_t local_port,uint32_t node,uint32_t from,
                            const uint8_t *,uint32_t);
void elden_qrtr_ports_fault(int);

EldenQrtrPort *elden_qrtr_port_open(uint64_t owner,uint64_t epoch,uint32_t service,uint32_t instance,
                                  const EldenQrtrClient *,void *client_context);
int elden_qrtr_port_level(EldenQrtrPort *,uint64_t owner,uint64_t epoch,EldenQrtrServer *);
int elden_qrtr_port_send(EldenQrtrPort *,uint64_t owner,uint64_t epoch,const EldenQrtrServer *,const uint8_t *,uint32_t);
int elden_qrtr_port_withdraw(EldenQrtrPort *,uint64_t owner,uint64_t epoch,const EldenQrtrServer *);
int elden_qrtr_port_status(EldenQrtrPort *,EldenQrtrPortStatus *);
#ifdef __cplusplus
}
#endif
#endif
