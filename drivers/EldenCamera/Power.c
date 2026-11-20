/* SPDX-License-Identifier: GPL-2.0-only */
#include "Power.h"
#include <stdbool.h>
#define BIT(n) (1u << (n))
#define WL2868C_LDO_ENABLE 0x0e
#define WL2868C_SYS_ENABLE BIT(7)
struct power {
    uint32_t magic, busy, ready, stopped, cleanup, snapshot;
    EldenCameraPowerIo io;
    uint8_t original_enable, observed_enable;
    uint8_t original_voltage[7], applied_voltage[7], users[7], active_users[7];
    uint32_t voltage_changed, enable_changed, sys_changed, sys_retained;
    int error;
};
_Static_assert(sizeof(struct power) <= sizeof(EldenCameraPower), "power storage");
struct regulator_desc { uint32_t enable_mask, enable_reg, enable_is_inverted, enable_val, disable_val; };
struct regulator_dev { struct power *regmap; const struct regulator_desc *desc; };

static int result(int rc) { return rc <= 0 ? rc : -13; }
static int admission(struct power *p) {
    return result(p->io.admit(p->io.context, p->io.owner, p->io.epoch, (int)p->cleanup));
}
static int read_byte(struct power *p, uint8_t reg, uint8_t *out) {
    int rc = admission(p);
    if (!rc) rc = result(p->io.read(p->io.context, reg, out, (int)p->cleanup));
    if (!rc && reg == WL2868C_LDO_ENABLE) p->observed_enable = *out;
    return rc;
}
static int write_byte(struct power *p, uint8_t reg, uint8_t value) {
    int rc = admission(p);
    if (!rc) rc = result(p->io.write(p->io.context, reg, value, (int)p->cleanup));
    if (rc) return rc;
    uint8_t actual = 0;
    rc = read_byte(p, reg, &actual);
    return rc ? rc : actual != value ? -71 : 0;
}
static int regmap_read(struct power *p, uint32_t reg, uint32_t *out) {
    uint8_t value = 0;
    int rc = read_byte(p, (uint8_t)reg, &value);
    if (!rc) *out = value;
    return rc;
}
static int regmap_update_bits(struct power *p, uint32_t reg, uint32_t mask, uint32_t value) {
    uint8_t current = 0;
    int rc = read_byte(p, (uint8_t)reg, &current);
    return rc ? rc : write_byte(p, (uint8_t)reg, (uint8_t)((current & ~mask) | (value & mask)));
}
#include "PowerRegulator.inc"
static struct power *core(EldenCameraPower *p) { return (struct power *)(void *)p; }
static int failure(struct power *p, int rc) {
    rc = rc < 0 ? rc : -13;
    p->ready = 0;
    if (!p->error) p->error = rc;
    return rc;
}
static int enter(EldenCameraPower *s) {
    if (!s || core(s)->magic != 0x574c3238) return -22;
    uint32_t zero = 0;
    return __atomic_compare_exchange_n(&core(s)->busy, &zero, 1u, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 0 : -16;
}
static int leave(struct power *p, int rc) {
    p->cleanup = 0;
    __atomic_store_n(&p->busy, 0u, __ATOMIC_RELEASE);
    return rc;
}
static unsigned rail(unsigned sensor, unsigned resource) {
    return resource == EldenCameraDovdd ? 6u : resource == EldenCameraDvdd ? sensor :
           resource == EldenCameraAvdd ? (sensor ? 2u : 3u) : 7u;
}
static const uint8_t selectors[7] = {79, 99, 164, 165, 163, 0, 37};
static uint8_t voltage_mask(unsigned index) { return index < 2 ? 0x7f : 0xff; }

/* Cleanup observes ambiguous enable/disable attempts without demanding that an
 * already disabled rail stay enabled. An uncertain reference stays held until
 * inactivity, actual disable and selector restoration all succeed. */
static int fresh(struct power *p) {
    uint8_t id = 0, enabled = 0;
    int rc = read_byte(p, 0, &id);
    if (rc) return rc;
    if (id != 0x82) return -19;
    rc = read_byte(p, WL2868C_LDO_ENABLE, &enabled);
    if (rc) return rc;
    if ((enabled & 0x7f) && !(enabled & WL2868C_SYS_ENABLE)) return -116;
    for (unsigned i = 0; i < 7; i++) {
        if (!p->users[i]) continue;
        uint8_t current = 0, mask = voltage_mask(i);
        rc = read_byte(p, (uint8_t)(i + 3), &current);
        if (rc) return rc;
        if (!p->cleanup) {
            if (!(enabled & BIT(i)) || (current & mask) != selectors[i]) return -116;
        } else if ((p->voltage_changed & BIT(i)) &&
                   (!(enabled & BIT(i)) || !p->active_users[i])) {
            if ((current & mask) != (p->applied_voltage[i] & mask) &&
                (current & mask) != (p->original_voltage[i] & mask)) return -116;
        } else if ((current & mask) != selectors[i]) {
            return -116;
        }
    }
    return 0;
}

int elden_camera_power_open(EldenCameraPower *s, const EldenCameraPowerIo *io) {
    if (!s || core(s)->magic || !io || !io->owner || !io->epoch || !io->admit ||
        !io->read || !io->write || !io->delay_us || !io->inactive || !io->rail_unused) return -22;
    int rc = result(io->admit(io->context, io->owner, io->epoch, 0));
    if (rc) return rc;
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof(*s); i++) b[i] = 0;
    struct power *p = core(s);
    p->magic = 0x574c3238;
    p->io = *io;
    uint8_t id = 0;
    rc = read_byte(p, 0, &id);
    if (rc || id != 0x82) return failure(p, rc ? rc : -19);
    rc = read_byte(p, WL2868C_LDO_ENABLE, &p->original_enable);
    if (rc) return failure(p, rc);
    if ((p->original_enable & 0x7f) && !(p->original_enable & WL2868C_SYS_ENABLE))
        return failure(p, -71);
    for (unsigned i = 0; i < 7; i++) {
        rc = read_byte(p, (uint8_t)(i + 3), &p->original_voltage[i]);
        if (rc) return failure(p, rc);
        p->applied_voltage[i] = p->original_voltage[i];
    }
    p->snapshot = 1;
    p->ready = 1;
    return 0;
}

static int disable_owned(struct power *p, unsigned index) {
    if (!(p->enable_changed & BIT(index))) return 0;
    if (p->original_enable & BIT(index)) return -116;
    int unused = result(p->io.rail_unused(p->io.context,p->io.owner,p->io.epoch,index+1));
    if (unused) return unused;
    uint8_t enabled = 0;
    int rc = read_byte(p, WL2868C_LDO_ENABLE, &enabled);
    if (rc) return rc;
    if (enabled & BIT(index)) {
        struct regulator_desc desc = {BIT(index), WL2868C_LDO_ENABLE, 0, 0, 0};
        struct regulator_dev rdev = {p, &desc};
        rc = regulator_disable_regmap(&rdev);
        if (rc) return rc;
    }
    /* A failed write/readback may already have disabled it. A fresh observed
     * absence allows another cleanup attempt without trusting cached state. */
    rc = read_byte(p, WL2868C_LDO_ENABLE, &enabled);
    if (rc || (enabled & BIT(index))) return rc ? rc : -71;
    p->enable_changed &= ~BIT(index);
    return 0;
}
static int restore_voltage(struct power *p, unsigned index) {
    if (!(p->voltage_changed & BIT(index))) return 0;
    int unused = result(p->io.rail_unused(p->io.context,p->io.owner,p->io.epoch,index+1));
    if (unused) return unused;
    uint8_t enabled = 0, value = 0, mask = voltage_mask(index);
    int rc = read_byte(p, WL2868C_LDO_ENABLE, &enabled);
    if (rc) return rc;
    if (enabled & BIT(index)) return -16; /* Never change a live rail's voltage. */
    rc = read_byte(p, (uint8_t)(index + 3), &value);
    if (rc) return rc;
    if ((value & mask) != (p->applied_voltage[index] & mask) &&
        (value & mask) != (p->original_voltage[index] & mask)) return -116;
    if ((value & mask) != (p->original_voltage[index] & mask)) {
        rc = write_byte(p, (uint8_t)(index + 3),
                        (uint8_t)((value & ~mask) | (p->original_voltage[index] & mask)));
        if (rc) return rc;
    }
    p->voltage_changed &= ~BIT(index);
    return 0;
}

int elden_camera_power_resource(EldenCameraPower *s, uint64_t owner, uint64_t epoch,
                                unsigned sensor, unsigned resource, int on) {
    if (sensor > 1 || (on != 0 && on != 1)) return -22;
    unsigned index = rail(sensor, resource);
    if (index == 7) return -22;
    int rc = enter(s);
    if (rc) return rc;
    struct power *p = core(s);
    if (owner != p->io.owner || epoch != p->io.epoch) return leave(p, -116);
    if (p->stopped) return leave(p, -125);
    p->cleanup = on ? 0u : 1u;
    if (on && !p->ready) return leave(p, p->error ? p->error : -125);
    if (!p->snapshot) return leave(p, p->error ? p->error : -125);
    rc = fresh(p);
    if (rc) return leave(p, failure(p, rc));
    uint8_t user = (uint8_t)BIT(sensor), mask = voltage_mask(index);
    if (on) {
        if (p->users[index] & user) return leave(p, 0); /* Fresh voltage/enable checked. */
        uint8_t current = 0;
        rc = read_byte(p, (uint8_t)(index + 3), &current);
        if (rc) return leave(p, failure(p, rc));
        bool enabled = (p->observed_enable & BIT(index)) != 0;
        if (enabled && (current & mask) != selectors[index]) return leave(p, failure(p, -34));
        if (!enabled && ((p->original_enable & BIT(index)) ||
                         (current & mask) != (p->original_voltage[index] & mask)))
            return leave(p, failure(p, -116)); /* Unowned target was changed by another owner. */
        if (!enabled) {
            rc = result(p->io.rail_unused(p->io.context,owner,epoch,index+1));
            if (rc) return leave(p,rc);
        }
        /* Ledger BEFORE any attempted write: an error is not non-execution. */
        p->users[index] |= user;
        if (!enabled) {
            p->applied_voltage[index] = (uint8_t)((current & ~mask) | selectors[index]);
            if ((current & mask) != selectors[index]) {
                p->voltage_changed |= BIT(index);
                rc = write_byte(p, (uint8_t)(index + 3), p->applied_voltage[index]);
                if (rc) return leave(p, failure(p, rc));
            }
            p->enable_changed |= BIT(index);
            if (!(p->observed_enable & WL2868C_SYS_ENABLE)) p->sys_changed = 1;
            struct regulator_desc desc = {BIT(index), WL2868C_LDO_ENABLE, 0, 0, 0};
            struct regulator_dev rdev = {p, &desc};
            rc = wl2868c_enable(&rdev);
            if (rc) return leave(p, failure(p, rc));
            rc = wl2868c_is_enabled(&rdev);
            if (rc != 1) return leave(p, failure(p, rc < 0 ? rc : -71));
        }
        rc = fresh(p);
        if (!rc) rc = result(p->io.delay_us(p->io.context, 150));
        if (rc) return leave(p, failure(p, rc));
        p->active_users[index] |= user;
        return leave(p, 0);
    }
    if (!(p->users[index] & user)) return leave(p, 0);
    rc = result(p->io.inactive(p->io.context, owner, epoch, sensor));
    if (rc) return leave(p, rc);
    if (p->users[index] & (uint8_t)~user) {
        uint8_t current = 0;
        rc = read_byte(p, (uint8_t)(index + 3), &current);
        if (rc || !(p->observed_enable & BIT(index)) || (current & mask) != selectors[index])
            return leave(p, failure(p, rc ? rc : -116));
        p->users[index] &= (uint8_t)~user;
        p->active_users[index] &= (uint8_t)~user;
        return leave(p, 0);
    }
    p->active_users[index] &= (uint8_t)~user;
    rc = disable_owned(p, index);
    if (!rc) rc = restore_voltage(p, index);
    if (rc) return leave(p, failure(p, rc));
    p->users[index] &= (uint8_t)~user;
    return leave(p, 0);
}

int elden_camera_power_stop(EldenCameraPower *s) {
    int rc = enter(s);
    if (rc) return rc;
    struct power *p = core(s);
    if (p->stopped) return leave(p, 0);
    p->cleanup = 1;
    rc = admission(p);
    if (rc) return leave(p, rc);
    for (unsigned sensor = 0; sensor < 2; sensor++) {
        rc = result(p->io.inactive(p->io.context, p->io.owner, p->io.epoch, sensor));
        if (rc) return leave(p, rc);
    }
    for (unsigned i = 0; i < 7; i++) if (p->users[i]) return leave(p, -16);
    if (p->snapshot) {
        rc = fresh(p);
        if (rc) return leave(p, failure(p, rc));
        for (unsigned i = 0; i < 7; i++) {
            rc = disable_owned(p, i);
            if (!rc) rc = restore_voltage(p, i);
            if (rc) return leave(p, failure(p, rc));
        }
        uint8_t enabled = 0;
        rc = read_byte(p, WL2868C_LDO_ENABLE, &enabled);
        if (rc) return leave(p, failure(p, rc));
        if (p->sys_changed && !(p->original_enable & WL2868C_SYS_ENABLE)) {
            if (!(enabled & 0x7f)) {
                if (enabled & WL2868C_SYS_ENABLE) rc = write_byte(p, WL2868C_LDO_ENABLE,
                    (uint8_t)(enabled & ~WL2868C_SYS_ENABLE));
                if (rc) return leave(p, failure(p, rc));
            } else p->sys_retained = 1; /* An unrelated live channel still needs SYS. */
        }
        p->sys_changed = 0;
    }
    p->ready = 0;
    p->stopped = 1;
    return leave(p, 0);
}
int elden_camera_power_status(EldenCameraPower *s, EldenCameraPowerStatus *out) {
    if (!out) return -22;
    int rc = enter(s);
    if (rc) return rc;
    struct power *p = core(s);
    EldenCameraPowerStatus value = {0};
    value.ready = p->ready; value.stopped = p->stopped;
    value.changed = p->voltage_changed || p->enable_changed || p->sys_changed;
    value.voltage_mask = p->voltage_changed; value.enable_mask = p->enable_changed;
    value.system_owned = p->sys_changed; value.system_retained_for_peer = p->sys_retained;
    value.original_enable = p->original_enable; value.observed_enable = p->observed_enable;
    value.error = p->error;
    for (unsigned i = 0; i < 7; i++) {
        value.users[i] = p->users[i];
        value.active_users[i] = p->active_users[i];
    }
    *out = value;
    return leave(p, 0);
}
