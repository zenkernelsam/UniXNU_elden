/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_CAMERA_BOARD_H
#define ELDEN_CAMERA_BOARD_H
#include "NativeClocks.h"
#include "PowerBinding.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void *context;uint64_t owner,epoch;
    /* Real same-board power/CCI/receiver/route authority. No success defaults. */
    int (*admit)(void *,uint64_t,uint64_t,unsigned sensor,unsigned operation);
    int (*delay_us)(void *,unsigned);
    int (*physical_drain)(void *,const EldenCameraPipelineConfig *);
    int (*buffer)(void *,const EldenCameraPipelineConfig *,const EldenCameraBuffer *,int complete);
    /* Original shared power owner holds exact CSIPHY3/4 0p88/1p2 supplies,
     * configured loads, AHB/HF interconnect votes and the early camera SID1c00
     * lease. action acquire/observe/release uses real readback and retirement. */
    int (*receiver_resources)(void *,const EldenCameraPipelineConfig *,int action);
} EldenCameraBoardOwner;
typedef struct {uint32_t hz[EldenCamClockCount];} EldenCameraBoardClockPlan;
typedef struct {
    EldenCameraNativeClocks *clocks;EldenCameraPowerSession *power;
    EldenCameraBoardOwner owner;EldenCameraPipelineConfig configs[2];
    uint64_t attempted[2];uint32_t rates[2][EldenCamClockCount];
    uint32_t domains[2],receivers[2],cci_open[2],pipeline_open[2],prepared[2],stopped[2];
    uint32_t initialized,cleanup_mask;EldenCameraPoweredCapture captures[2];int error;
} EldenCameraBoard;
/* Caller owns one sleepable camera executor across both targets and board
 * callbacks; no automatic startup, independent power bus or physical tests.
 * Before prepare, original native clock graph and shared WL session open,
 * pins prepare, same owner/epoch. CSI/SMMU resources must have real authority. */
int elden_camera_board_bind(EldenCameraBoard *,EldenCameraNativeClocks *,EldenCameraPowerSession *,const EldenCameraBoardOwner *);
int elden_camera_board_prepare(EldenCameraBoard *,const EldenCameraPipelineConfig *,const EldenCameraBoardClockPlan *);
int elden_camera_board_probe(EldenCameraBoard *,unsigned sensor);
int elden_camera_board_start(EldenCameraBoard *,unsigned sensor);
int elden_camera_board_stop(EldenCameraBoard *,unsigned sensor);
/* Called after both real sensor/native CCI/pipeline stops; final clock/pin and
 * shared WL session retirement remains explicit to the original recovery owner. */
int elden_camera_board_shutdown(EldenCameraBoard *);
#ifdef __cplusplus
}
#endif
#endif
