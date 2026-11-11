/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {ELDEN_UCSI_OWNER=32779, ELDEN_UCSI_V1_BUFFER=48,
      ELDEN_UCSI_V2_BUFFER=528, ELDEN_UCSI_MAX_MESSAGE=544};
enum EldenUcsiPhase {EldenUcsiOff, EldenUcsiVersion, EldenUcsiQueryWrite,
    EldenUcsiQueryRead, EldenUcsiAckWrite, EldenUcsiAckRead, EldenUcsiReady, EldenUcsiFailed,
    EldenUcsiClearWrite,EldenUcsiClearRead,EldenUcsiResetWrite,EldenUcsiResetRead,
    EldenUcsiNotifyWrite,EldenUcsiNotifyRead,EldenUcsiEventsWrite,EldenUcsiEventsRead};
enum EldenUcsiFailure {EldenUcsiFailureNone,EldenUcsiFailureResponse,
    EldenUcsiFailureTimeout,EldenUcsiFailurePolling,EldenUcsiFailureProtocol};
typedef struct {
    uint32_t flags, rdo, capability;
    uint8_t status_bytes, capability_bytes, bc_status, power_mode, partner;
    bool status_valid, capability_valid, connected, source, usb;
    bool orientation_known, reversed, sink_path_known, sink_path_enabled;
    uint32_t source_pdos[7];uint8_t pdo_count;
    bool pdos_complete,pdos_consistent,pdos_error;
} EldenUcsiPort;
typedef struct {
    EldenUcsiPort port[2];
    uint32_t attributes, cci, cycle, epoch;
    uint16_t features, version;
    uint8_t connectors;
} EldenUcsiSnapshot;
typedef struct {
    EldenUcsiSnapshot working, committed;
    enum EldenUcsiPhase phase;
    uint32_t epoch, cycles, ignored, errors, timeouts, notifications;
    uint32_t buffer_bytes, cci, polls, firmware_error, command_status;
    unsigned query;
    bool up, pending, polling, refresh_wanted, bootstrap_ack, command_error;
    bool initialize_ppm,notify_ack;
    bool connector_events,events_ready,events_ack;
    uint8_t active_connector,ack_connector;
    uint32_t connector_acks;
    bool query_pdos;
    uint8_t pdo_disabled;
    enum EldenUcsiPhase failed_phase;
    enum EldenUcsiFailure failure;
    uint32_t last_opcode,last_reply_bytes;
    uint64_t last_control;
    uint32_t notification_control;
} EldenUcsiQuery;
enum {EldenUcsiIgnored=0,EldenUcsiProgress=1,EldenUcsiCycle=2,EldenUcsiNotification=3,EldenUcsiStopped=-1};

/* Single executor; normal cycles serialize GET/ACK without role or sink-path
 * changes. Explicit initialize_ppm performs the one-time stock PPM sequence.
 * Optional connector_events completes the Linux notification setup and
 * acknowledges a connector event only after its own successful status read.
 */
void elden_ucsi_init(EldenUcsiQuery *query);
/* Explicit board bring-up only, before link-up. Uses the stock v1 PPM reset
 * and basic command/error notification sequence; no connector reset/role/sink.
 */
void elden_ucsi_initialize_ppm(EldenUcsiQuery *query,bool enable);
void elden_ucsi_enable_events(EldenUcsiQuery *query,bool enable);
void elden_ucsi_enable_pdos(EldenUcsiQuery *query,bool enable);
void elden_ucsi_link(EldenUcsiQuery *query,bool up);
void elden_ucsi_refresh(EldenUcsiQuery *query);
int elden_ucsi_next(EldenUcsiQuery *query,uint8_t *out,size_t capacity);
void elden_ucsi_unsent(EldenUcsiQuery *query);
void elden_ucsi_timeout(EldenUcsiQuery *query);
void elden_ucsi_tick(EldenUcsiQuery *query);
int elden_ucsi_receive(EldenUcsiQuery *query,const uint8_t *data,size_t bytes);
uint64_t elden_ucsi_query_control(unsigned index);
uint64_t elden_ucsi_pdo_control(unsigned connector,unsigned offset,unsigned count);
uint64_t elden_ucsi_event_control(const EldenUcsiQuery *query);
#ifdef __cplusplus
}
#endif
