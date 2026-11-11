/* SPDX-License-Identifier: GPL-2.0-only */
/* Host vectors use the actual pinned TB322FC firmware inputs, without a UART. */
#include "Wcn7861Peripheral.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void put16(uint8_t *p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb"); CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long bytes = ftell(f); CHECK(bytes > 0 && bytes <= 65536);
    *size = (size_t)bytes;
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *data = malloc(*size); CHECK(data != NULL);
    CHECK(fread(data, 1, *size, f) == *size); CHECK(fclose(f) == 0);
    return data;
}
static void unchanged_plan(enum EldenWcnPeripheralFirmware kind,
    const uint8_t *data, size_t size) {
    EldenWcnPeripheralPlan plan, previous;
    memset(&plan, 0xa5, sizeof(plan)); memcpy(&previous, &plan, sizeof(plan));
    CHECK(elden_wcn_peripheral_prepare(kind, data, size, &plan) != 0);
    CHECK(memcmp(&plan, &previous, sizeof(plan)) == 0);
}
static void fixture(enum EldenWcnPeripheralFirmware kind, const char *path) {
    size_t size; uint8_t *data = read_file(path, &size);
    uint8_t *original = malloc(size), *assembled = calloc(size, 1), *expected = malloc(size);
    CHECK(original && assembled && expected);
    memcpy(original, data, size); memcpy(expected, data, size);
    EldenWcnPeripheralPlan plan;
    CHECK(elden_wcn_peripheral_prepare(kind, data, size, &plan) == 0);
    CHECK(plan.data == data && plan.size == size && plan.kind == kind);
    if (kind == EldenWcnNvm) {
        CHECK(plan.baud_byte < size && plan.ibs_byte < size);
        expected[plan.baud_byte] = 0x15; expected[plan.ibs_byte] |= 1;
    }
    for (size_t offset = 0; offset < size; offset += ELDEN_WCN_PERIPHERAL_SEGMENT) {
        uint8_t packet[ELDEN_WCN_PERIPHERAL_PACKET]; size_t bytes = 999; bool wait = false;
        CHECK(elden_wcn_peripheral_segment(&plan, offset, packet, sizeof(packet), &bytes, &wait) == 0);
        size_t payload = size - offset; if (payload > 243) payload = 243;
        CHECK(bytes == payload + 8 && packet[0] == 0x31 && packet[1] == 0 &&
            packet[2] == 0xf0 && packet[3] == 0xff && packet[4] == payload + 3 &&
            packet[5] == 6 && packet[6] == (kind == EldenWcnTmel ? 3 : 0) && packet[7] == payload);
        CHECK(wait == (kind == EldenWcnNvm || offset + payload == size));
        memcpy(assembled + offset, packet + 8, payload);
    }
    CHECK(memcmp(assembled, expected, size) == 0);
    CHECK(memcmp(data, original, size) == 0);
    uint8_t packet[251], sentinel[251]; memset(packet, 0xa5, sizeof(packet)); memcpy(sentinel, packet, sizeof(packet));
    size_t bytes = 99; bool wait = true;
    CHECK(elden_wcn_peripheral_segment(&plan, 0, packet, 1, &bytes, &wait) == EldenWcnPeripheralBuffer);
    CHECK(bytes == 99 && wait && memcmp(packet, sentinel, sizeof(packet)) == 0);
    CHECK(elden_wcn_peripheral_segment(&plan, 1, packet, sizeof(packet), &bytes, &wait) != 0);
    CHECK(elden_wcn_peripheral_segment(&plan, 0, data, size, &bytes, &wait) != 0);
    CHECK(memcmp(data, original, size) == 0);
    unchanged_plan(kind, data, 23);
    uint8_t *bad = malloc(size); CHECK(bad != NULL); memcpy(bad, data, size);
    bad[0] ^= 1; unchanged_plan(kind, bad, size); memcpy(bad, data, size);
    if (kind == EldenWcnNvm) {
        put16(bad + plan.baud_byte - 13, 16); unchanged_plan(kind, bad, size);
        memcpy(bad, data, size); put16(bad + plan.ibs_byte - 13, 17); unchanged_plan(kind, bad, size);
        memcpy(bad, data, size); bad[size - 11] = 0xff; bad[size - 10] = 0xff;
        unchanged_plan(kind, bad, size); /* a malformed late tag refuses the entire file */
    } else if (kind == EldenWcnTmel) {
        put16(bad + 18, 40); unchanged_plan(kind, bad, size);
        memcpy(bad, data, size); put32(bad + 28, UINT32_MAX); unchanged_plan(kind, bad, size);
    } else {
        bad[16] ^= 1; unchanged_plan(kind, bad, size);
    }
    free(bad); free(expected); free(assembled); free(original); free(data);
}
static void replies(void) {
    uint8_t packet[25] = {0x34, 0, 0xff, 21, 0xf0, 1};
    uint8_t *d = packet + 1;
    put16(d + 6, 0xfff0); d[9] = 5; d[10] = 0;
    put32(d + 11, 0x21); put16(d + 17, 0x200); put32(d + 19, 0x40210200);
    EldenWcnPeripheralExpected query = {0xfff0, 5, 0}; EldenWcnPeripheralReply result;
    CHECK(elden_wcn_peripheral_reply(query, packet, sizeof(packet), &result) == 0);
    CHECK(result.supported_v2 && result.status == 0 && result.product == 0x21 && result.rom == 0x200);
    for (size_t size = 0; size < sizeof(packet); ++size) {
        EldenWcnPeripheralReply previous; memset(&result, 0xa5, sizeof(result)); memcpy(&previous, &result, sizeof(result));
        uint8_t truncated[25]; memcpy(truncated, packet, sizeof(packet));
        if (size >= 4) truncated[3] = (uint8_t)(size - 4);
        CHECK(elden_wcn_peripheral_reply(query, truncated, size, &result) != 0);
        CHECK(memcmp(&result, &previous, sizeof(result)) == 0);
    }
    query.subsystem = 3; CHECK(elden_wcn_peripheral_reply(query, packet, sizeof(packet), &result) == EldenWcnPeripheralMismatch);
    query.subsystem = 0; d[8] = 2;
    CHECK(elden_wcn_peripheral_reply(query, packet, sizeof(packet), &result) == 0 && result.status == 2 && !result.supported_v2);
    d[8] = 0; put32(d + 19, 0x40210300);
    CHECK(elden_wcn_peripheral_reply(query, packet, sizeof(packet), &result) == 0 && !result.supported_v2);
    uint8_t status[10] = {0x34, 0, 0xff, 6, 0xf0, 0, 0, 0, 0xf1, 0xff};
    query.opcode = 0xfff1; query.subopcode = 0;
    CHECK(elden_wcn_peripheral_reply(query, status, sizeof(status), &result) == 0 && result.status == 0);
    uint8_t note[8] = {0x34, 0, 0xff, 4, 0xf0, 3, 0, 0};
    CHECK(elden_wcn_peripheral_notification(note, sizeof(note)) == 1);
    note[7] = 3; CHECK(elden_wcn_peripheral_notification(note, sizeof(note)) == 2);
    note[5] = 2; note[6] = 1; note[7] = 1; CHECK(elden_wcn_peripheral_notification(note, sizeof(note)) == 3);
    note[3] = 3; CHECK(elden_wcn_peripheral_notification(note, sizeof(note)) == 0);
    for (uint32_t product = 0x1e; product <= 0x21; ++product) {
        CHECK(elden_wcn_uses_current_baud(product, 0x100, 0x40210100) == (product == 0x1e || product == 0x21));
        CHECK(elden_wcn_uses_current_baud(product, 0x200, 0x40210200) == (product == 0x1e || product == 0x21));
        CHECK(!elden_wcn_uses_current_baud(product, 0x100, 0x40210200));
    }
}
int main(int argc, char **argv) {
    CHECK(argc == 4);
    fixture(EldenWcnPatch, argv[1]); fixture(EldenWcnNvm, argv[2]); fixture(EldenWcnTmel, argv[3]);
    replies();
    for (unsigned control = 0; control <= EldenWcnActivate; ++control) {
        uint8_t bytes[8]; size_t size = 0;
        CHECK(elden_wcn_peripheral_control((enum EldenWcnPeripheralControl)control, bytes, sizeof(bytes), &size) == 0);
        CHECK(bytes[0] == 0x31 && bytes[1] == 0 && size == (size_t)bytes[4] + 5);
    }
    uint8_t bytes[8], previous[8]; memset(bytes, 0xa5, sizeof(bytes)); memcpy(previous, bytes, sizeof(bytes)); size_t size = 123;
    CHECK(elden_wcn_peripheral_control((enum EldenWcnPeripheralControl)99, bytes, sizeof(bytes), &size) != 0);
    CHECK(size == 123 && memcmp(bytes, previous, sizeof(bytes)) == 0);
    printf("PASS: %u checks; actual3 firmware formats/immutable segmentation/reply identity; no hardware operations\n", checks);
    return 0;
}
