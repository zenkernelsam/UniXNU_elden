/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_CAMERA_SHARED_I2C10_H
#define ELDEN_CAMERA_SHARED_I2C10_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Actual board/boot identity, PMIC reset deasserted/input supplies and
     * original I2C10 ownership; nonsleeping while shared serial mutex held.
     * cleanup is independent of public power/sample admission. */
    int (*admit)(void *,uint64_t owner,uint64_t epoch,int cleanup);
    /* Original owner proves both sensor/receiver inactivity and that its WL
     * power ledger has actually stopped/restored. Called under the same serial
     * mutex; must not call back into I2C or sleep. Admission alone is NOT this
     * retirement proof. Failure retains the borrow and controller to reset. */
    int (*retired)(void *,uint64_t owner,uint64_t epoch);
} EldenCameraI2c10Authority;
typedef struct EldenCameraI2c10Port EldenCameraI2c10Port;
int elden_camera_i2c10_open(const EldenCameraI2c10Authority *,EldenCameraI2c10Port **);
/* Only WL2868C address0x2f, ID/read, voltage selectors3..9 and enable0x0e.
 * Uses the already acquired original backlight/SE2 engine and its mutex.
 * No second MMIO/GENI controller, raw app IOCTL or new firmware/clock setup. */
int elden_camera_i2c10_read(EldenCameraI2c10Port *,uint64_t owner,uint64_t epoch,uint8_t reg,uint8_t *,int cleanup);
int elden_camera_i2c10_write(EldenCameraI2c10Port *,uint64_t owner,uint64_t epoch,uint8_t reg,uint8_t,int cleanup);
int elden_camera_i2c10_close(EldenCameraI2c10Port *,uint64_t owner,uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
