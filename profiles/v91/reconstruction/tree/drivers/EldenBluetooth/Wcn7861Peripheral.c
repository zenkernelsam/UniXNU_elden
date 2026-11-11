/* SPDX-License-Identifier: GPL-2.0-only */
/* Peripheral wire/firmware rules adapted from Armada TB322FC patch0227,
 * GEEKiDoS <geek_ds@foxmail.com>, and h0cheung's board initialization:
 * dc7d14dc64c8d4f86220b30cd126b17d805c8474
 * ac6ae99d08b70ffad9c6733967e0ca8f52e1751d
 * Fixed reference/firmware bytes remain in the consuming workspace's research.
 * This module deliberately contains no transport/startup or automatic initializer.
 */
#include "Wcn7861Peripheral.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int elden_wcn_peripheral_prepare(enum EldenWcnPeripheralFirmware kind,
    const uint8_t *data, size_t size, EldenWcnPeripheralPlan *out) {
    if (!data || !out || kind < EldenWcnPatch || kind > EldenWcnTmel)
        return EldenWcnPeripheralArgument;
    if (size < 24 || size > 65536) return EldenWcnPeripheralFormat;
    EldenWcnPeripheralPlan plan = {data, size, kind, SIZE_MAX, SIZE_MAX};
    if (kind == EldenWcnTmel) {
        /* The exact Peach20 TME-L asset is ELF32/little-endian RISC-V,
         * not ARM/Hexagon. Verify the container without altering its bytes. */
        if (size < 52 || memcmp(data, "\177ELF", 4) || data[4] != 1 ||
            data[5] != 1 || data[6] != 1 || le16(data + 18) != 243 ||
            le16(data + 40) != 52 || le16(data + 42) != 32)
            return EldenWcnPeripheralFormat;
        size_t start = le32(data + 28), count = le16(data + 44);
        if (!count || start > size || count > (size - start) / 32)
            return EldenWcnPeripheralFormat;
        for (size_t index = 0; index < count; ++index) {
            const uint8_t *ph = data + start + index * 32;
            size_t offset = le32(ph + 4), bytes = le32(ph + 16);
            if (offset > size || bytes > size - offset)
                return EldenWcnPeripheralFormat;
            if (le32(ph) == 1 && bytes > le32(ph + 20))
                return EldenWcnPeripheralFormat;
        }
    } else {
        if (data[0] != (kind == EldenWcnNvm ? 2 : 1) ||
            (le32(data) >> 8) != size - 4) return EldenWcnPeripheralFormat;
        if (kind == EldenWcnPatch) {
            if (le16(data + 16) != 0x21 || le16(data + 18) != 0x200 || data[14] != 3)
                return EldenWcnPeripheralFormat;
        } else {
            for (size_t offset = 4; offset < size;) {
                if (size - offset < 12) return EldenWcnPeripheralFormat;
                unsigned tag = le16(data + offset);
                size_t bytes = le16(data + offset + 2);
                if (bytes > size - offset - 12 || ((tag == 17 || tag == 27) && bytes < 2))
                    return EldenWcnPeripheralFormat;
                if (tag == 17) {
                    if (plan.baud_byte != SIZE_MAX) return EldenWcnPeripheralFormat;
                    plan.baud_byte = offset + 13;
                }
                if (tag == 27) {
                    if (plan.ibs_byte != SIZE_MAX) return EldenWcnPeripheralFormat;
                    plan.ibs_byte = offset + 13;
                }
                offset += 12 + bytes;
            }
            /* Exact board NVM must supply both controls; do not call an
             * incomplete generic NVM successfully initialized. */
            if (plan.baud_byte == SIZE_MAX || plan.ibs_byte == SIZE_MAX)
                return EldenWcnPeripheralFormat;
        }
    }
    *out = plan;
    return EldenWcnPeripheralOk;
}

int elden_wcn_peripheral_segment(const EldenWcnPeripheralPlan *plan, size_t offset,
    uint8_t *out, size_t capacity, size_t *packet_bytes, bool *wait_for_reply) {
    if (!plan || !plan->data || !out || !packet_bytes || !wait_for_reply ||
        !plan->size || plan->size > 65536 || offset >= plan->size ||
        offset % ELDEN_WCN_PERIPHERAL_SEGMENT ||
        plan->kind < EldenWcnPatch || plan->kind > EldenWcnTmel)
        return EldenWcnPeripheralArgument;
    if (plan->kind == EldenWcnNvm &&
        (plan->baud_byte >= plan->size || plan->ibs_byte >= plan->size))
        return EldenWcnPeripheralArgument;
    size_t bytes = plan->size - offset;
    if (bytes > ELDEN_WCN_PERIPHERAL_SEGMENT) bytes = ELDEN_WCN_PERIPHERAL_SEGMENT;
    if (capacity < bytes + 8) return EldenWcnPeripheralBuffer;
    uintptr_t source = (uintptr_t)plan->data, target = (uintptr_t)out;
    if (source > UINTPTR_MAX - plan->size || target > UINTPTR_MAX - bytes - 8 ||
        (source < target + bytes + 8 && target < source + plan->size))
        return EldenWcnPeripheralArgument;
    uint8_t packet[ELDEN_WCN_PERIPHERAL_PACKET] = {0x31, 0, 0xf0, 0xff, 0, 6, 0, 0};
    packet[4] = (uint8_t)(bytes + 3);
    packet[6] = plan->kind == EldenWcnTmel ? 3 : 0;
    packet[7] = (uint8_t)bytes;
    memcpy(packet + 8, plan->data + offset, bytes);
    if (plan->kind == EldenWcnNvm) {
        if (plan->baud_byte >= offset && plan->baud_byte - offset < bytes)
            packet[8 + plan->baud_byte - offset] = 0x15;
        if (plan->ibs_byte >= offset && plan->ibs_byte - offset < bytes)
            packet[8 + plan->ibs_byte - offset] |= 1;
    }
    memcpy(out, packet, bytes + 8);
    *packet_bytes = bytes + 8;
    *wait_for_reply = plan->kind == EldenWcnNvm || offset + bytes == plan->size;
    return EldenWcnPeripheralOk;
}

int elden_wcn_peripheral_control(enum EldenWcnPeripheralControl control,
    uint8_t *out, size_t capacity, size_t *packet_bytes) {
    static const uint8_t commands[][8] = {
        {0x31, 0, 0xf0, 0xff, 2, 5, 0},
        {0x31, 0, 0xf1, 0xff, 2, 2, 0x15},
        {0x31, 0, 0xf0, 0xff, 3, 8, 0, 0},
        {0x31, 0, 0xf1, 0xff, 1, 3},
        {0x31, 0, 0xf0, 0xff, 3, 8, 3, 0},
        {0x31, 0, 0xf0, 0xff, 2, 5, 3},
        {0x31, 0, 0xf1, 0xff, 3, 0, 1, 1}
    };
    static const uint8_t lengths[] = {7, 7, 8, 6, 8, 7, 8};
    if (!out || !packet_bytes || control < EldenWcnVersion || control > EldenWcnActivate)
        return EldenWcnPeripheralArgument;
    size_t bytes = lengths[control];
    if (capacity < bytes) return EldenWcnPeripheralBuffer;
    memcpy(out, commands[control], bytes);
    *packet_bytes = bytes;
    return EldenWcnPeripheralOk;
}

static bool event_valid(const uint8_t *packet, size_t size) {
    return packet && size >= 8 && size <= 259 && packet[0] == 0x34 &&
        packet[1] == 0 && packet[2] == 0xff && packet[3] == size - 4 && packet[4] == 0xf0;
}

int elden_wcn_peripheral_reply(EldenWcnPeripheralExpected expected,
    const uint8_t *packet, size_t size, EldenWcnPeripheralReply *out) {
    if (!out || (expected.opcode != 0xfff0 && expected.opcode != 0xfff1))
        return EldenWcnPeripheralArgument;
    if (!event_valid(packet, size)) return EldenWcnPeripheralFormat;
    const uint8_t *data = packet + 1;
    size_t length = size - 1;
    EldenWcnPeripheralReply reply;
    memset(&reply, 0, sizeof(reply));
    if (expected.opcode == 0xfff1 && expected.subopcode == 0 &&
        length >= 9 && data[4] == 0 && le16(data + 7) == 0xfff1) {
        reply.status = data[5];
    } else {
        if (length < 10 || data[4] != 1 || le16(data + 6) != expected.opcode ||
            data[9] != expected.subopcode) return EldenWcnPeripheralMismatch;
        if (expected.opcode == 0xfff0) {
            if (length < 11 || data[10] != expected.subsystem) return EldenWcnPeripheralMismatch;
            if (expected.subopcode == 5) {
                if (length < 24) return EldenWcnPeripheralFormat;
                reply.product = le32(data + 11);
                reply.rom = le16(data + 17);
                reply.soc = le32(data + 19);
                reply.supported_v2 = data[8] == 0 && (reply.product == 0x21 || reply.product == 0x1e) &&
                    reply.rom == 0x200 && reply.soc == 0x40210200;
            }
        }
        reply.status = data[8];
    }
    *out = reply;
    return EldenWcnPeripheralOk;
}

unsigned elden_wcn_peripheral_notification(const uint8_t *packet, size_t size) {
    if (!event_valid(packet, size)) return 0;
    const uint8_t *data = packet + 1;
    if (data[4] == 3 && data[5] == 0 && data[6] == 0) return 1;
    if (data[4] == 3 && data[5] == 0 && data[6] == 3) return 2;
    if (data[4] == 2 && data[5] == 1 && data[6] == 1) return 3;
    return 0;
}

bool elden_wcn_uses_current_baud(uint32_t product, uint16_t rom, uint32_t soc) {
    return ((soc == 0x40210100 && rom == 0x100) || (soc == 0x40210200 && rom == 0x200)) &&
        (product == 0x1e || product == 0x21);
}
