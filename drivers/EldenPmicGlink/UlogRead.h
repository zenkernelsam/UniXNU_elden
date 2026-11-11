/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ELDEN_ULOG_OWNER 32778u
#define ELDEN_ULOG_PAYLOAD 8192u
#define ELDEN_ULOG_REPLY (12u + ELDEN_ULOG_PAYLOAD)
#define ELDEN_ULOG_INIT_PAGES 8u
#define ELDEN_ULOG_CURRENT_PAGES 4u
#define ELDEN_ULOG_MAX_PAGES (ELDEN_ULOG_INIT_PAGES + ELDEN_ULOG_CURRENT_PAGES)
enum { EldenUlogLength=1, EldenUlogTuple, EldenUlogTimeout, EldenUlogOverflow, EldenUlogStopped, EldenUlogDuplicate };
/* Static/heap storage only in the native worker: never put this on its stack.
 * All calls are serialized by the owner. Default mode queues two GETs;
 * optional pagination caps the boot at eight init plus four current pages.
 * Neither mode has a setter; quotas survive transport epoch changes. */
typedef struct {
    bool up, pending, failed, ready;
    bool paging, init_empty, current_empty, policy_trace;
    uint32_t epoch, requests, completed, error, opcode, last_opcode, crc;
    uint32_t init_pages, current_pages, text_bytes;
    uint8_t payload[ELDEN_ULOG_PAYLOAD];
} EldenUlogRead;
void elden_ulog_init(EldenUlogRead *s);
/* Before connecting only; existing two-page behavior remains the default. */
void elden_ulog_enable_paging(EldenUlogRead *s);
/* Twelve spaced current pages; the caller supplies the bounded schedule.
 * Empty pages do not end the window; quotas still survive link changes. */
void elden_ulog_enable_policy_trace(EldenUlogRead *s);
void elden_ulog_link(EldenUlogRead *s, bool up, uint32_t epoch);
int elden_ulog_next(EldenUlogRead *s, uint8_t request[16]);
/* Only for failure before the transport queues any request. */
void elden_ulog_unsent(EldenUlogRead *s);
void elden_ulog_fail(EldenUlogRead *s, unsigned reason);
int elden_ulog_receive(EldenUlogRead *s, uint32_t epoch, const uint8_t *p, size_t bytes);
void elden_ulog_release(EldenUlogRead *s);
uint32_t elden_ulog_crc(const uint8_t *p, size_t bytes);
#ifdef __cplusplus
}
#endif
