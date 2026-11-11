/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* TB322FC's Brahma v2 peripheral protocol, before normal HCI startup.
 * These are pure byte codecs. They perform no UART, power or firmware writes.
 * The future UART owner must serialize commands and quarantine a timed-out
 * transaction: this wire protocol has no nonce to distinguish a late reply.
 */
enum EldenWcnPeripheralError {
    EldenWcnPeripheralOk = 0,
    EldenWcnPeripheralArgument = -1,
    EldenWcnPeripheralFormat = -2,
    EldenWcnPeripheralBuffer = -3,
    EldenWcnPeripheralMismatch = -4
};
enum EldenWcnPeripheralFirmware { EldenWcnPatch, EldenWcnNvm, EldenWcnTmel };
enum EldenWcnPeripheralControl {
    EldenWcnVersion, EldenWcnBaud8M, EldenWcnArbitrate, EldenWcnReset,
    EldenWcnSelectTmel, EldenWcnTmelVersion, EldenWcnActivate
};
enum { ELDEN_WCN_PERIPHERAL_SEGMENT = 243, ELDEN_WCN_PERIPHERAL_PACKET = 251 };
typedef struct {
    const uint8_t *data; /* borrowed immutable buffer; retained through download */
    size_t size;
    enum EldenWcnPeripheralFirmware kind;
    size_t baud_byte, ibs_byte;
} EldenWcnPeripheralPlan;
typedef struct {
    uint16_t opcode;
    uint8_t subopcode, subsystem;
} EldenWcnPeripheralExpected;
typedef struct {
    uint8_t status;
    bool supported_v2;
    uint32_t product, soc;
    uint16_t rom;
} EldenWcnPeripheralReply;

/* Full firmware validation precedes the first possible outgoing segment.
 * NVM changes are made only in outgoing copies; original files stay intact.
 * Firmware integrity/selection must additionally pass the exact board manifest.
 */
int elden_wcn_peripheral_prepare(enum EldenWcnPeripheralFirmware kind,
    const uint8_t *data, size_t size, EldenWcnPeripheralPlan *out);
int elden_wcn_peripheral_segment(const EldenWcnPeripheralPlan *plan, size_t offset,
    uint8_t *out, size_t capacity, size_t *packet_bytes, bool *wait_for_reply);
int elden_wcn_peripheral_control(enum EldenWcnPeripheralControl control,
    uint8_t *out, size_t capacity, size_t *packet_bytes);
/* A complete 0x34 wire packet, including type; H4 framing stays with the UART owner. */
int elden_wcn_peripheral_reply(EldenWcnPeripheralExpected expected,
    const uint8_t *packet, size_t size, EldenWcnPeripheralReply *out);
/* 1 patch-ready, 2 TME-L-ready, 3 activation-ready, 0 unrelated/malformed. */
unsigned elden_wcn_peripheral_notification(const uint8_t *packet, size_t size);
bool elden_wcn_uses_current_baud(uint32_t product, uint16_t rom, uint32_t soc);

#ifdef __cplusplus
}
#endif
