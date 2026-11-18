/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_CORE_H
#define ELDEN_CAMERA_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenCameraRear=0,EldenCameraFront=1};
enum EldenCameraOperation {EldenCameraAcquire=0,EldenCameraConfigure=1,EldenCameraStream=2,EldenCameraCleanup=3};
enum EldenCameraResource {EldenCameraReset=0,EldenCameraClock=1,EldenCameraDovdd=2,EldenCameraDvdd=3,EldenCameraAvdd=4};
enum {EldenCameraArgument=-22,EldenCameraBusy=-16,EldenCameraMissing=-19,EldenCameraHeld=-125};
typedef struct {
    void *context;
    /* Requires exact board wiring/resource/epoch qualification BEFORE power
     * or a register transaction. DT control labels alone do not qualify it. */
    int (*admit)(void *,unsigned sensor,enum EldenCameraOperation);
    int (*resource)(void *,unsigned sensor,enum EldenCameraResource,int enable);
    /* Big-endian 16-bit register addresses; values are8/16-bit big-endian.
     * Individual repeated Samsung6f12 port writes MUST NOT auto-increment. */
    int (*read)(void *,unsigned master,uint8_t slave,uint16_t reg,unsigned width,uint32_t *);
    int (*write)(void *,unsigned master,uint8_t slave,uint16_t reg,unsigned width,uint32_t value);
    int (*delay_us)(void *,unsigned);
    /* Actual CSI/VFE/SMMU/buffer/IRQ receiver prepared/idle proofs. Neither
     * is replaced with a success stub or inferred from a sensor ID. */
    int (*receiver_ready)(void *,unsigned sensor);
    int (*receiver_drain)(void *,unsigned sensor);
} EldenCameraIo;
typedef union {uint64_t alignment;uint8_t bytes[1024];} EldenCameraCore;
typedef struct {uint32_t width,height,link_hz,pixel_hz;uint16_t expected_sensor_id,rotation;
    uint8_t master,slave,reset_gpio,mclk,lanes,raw_bits,bayer,reserved;} EldenCameraMode;
enum {EldenCameraGrbg=0,EldenCameraRggb=1};
/* Zero-initialize once; never overwrite an owned/held context. No allocation,
 * IOKit publication or device action occurs in bind. The consumer serializes
 * callbacks; actual API admission additionally excludes reentrant calls. */
int elden_camera_bind(EldenCameraCore *,const EldenCameraIo *,unsigned sensor);
int elden_camera_probe(EldenCameraCore *); /*actual ID + real power-off proof*/
int elden_camera_start(EldenCameraCore *); /*requires actual receiver ready*/
int elden_camera_control(EldenCameraCore *,uint32_t exposure,uint32_t analogue_gain);
int elden_camera_stop(EldenCameraCore *); /*receiver drain before power release*/
int elden_camera_mode(EldenCameraCore *,EldenCameraMode *);
int elden_camera_terminal(EldenCameraCore *); /*owned sensor/receiver control stop only*/
#ifdef __cplusplus
}
#endif
#endif
