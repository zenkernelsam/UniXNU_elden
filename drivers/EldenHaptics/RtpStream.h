/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_HAPTICS_RTP_STREAM_H
#define ELDEN_HAPTICS_RTP_STREAM_H
#include "AwRtp.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenRtpIdle=0,EldenRtpStarting=1,EldenRtpPlaying=2,EldenRtpComplete=3,EldenRtpHeld=4,EldenRtpFailed=5,EldenRtpCancelled=6};
typedef struct {
    uint32_t max_duration_us,max_service_gap_us,measured_min_payload_bps,window_frames;
    uint64_t max_window_squares[2];uint32_t max_window_on_frames[2];
    uint16_t max_sum_gain;uint8_t max_gain[2],max_sample[2],mask;
} EldenRtpLimits;
typedef struct {const int8_t *pcm[2];size_t frames;uint32_t rate_hz;uint8_t mask,gain[2];uint64_t id;} EldenRtpClip;
typedef struct {
    void *context;
    int (*admit)(void *,uint64_t id,int cleanup);
    /* Native's persistent RAM/RTP arbiter reserves these worst rolling-window
     * costs until clip end+window. No per-clip budget reset or caller flag.
     * This callback is mandatory, not a placeholder for a missing provider. */
    int (*reserve)(void *,uint64_t id,uint32_t duration_us,const uint64_t peak_squares[2],const uint32_t peak_on_frames[2]);
    uint64_t (*now_us)(void *);
} EldenRtpIo;
typedef struct {
    EldenRtpIo io;EldenRtpLimits limits;EldenRtpClip clip;EldenAwRtp motor[2];
    size_t sent[2];uint64_t start_us,last_us,startup_deadline;uint32_t state;
    uint8_t active,seen,turn;int error;
} EldenRtpStream;
/* Clip memory/profile are retained immutable throughout playback. All source
 * conversion occurs before GO. No uint8 gain/PCM interface reaches userspace.
 * Limits/min bandwidth must be measured for the actual GENI/pins/clock path.
 * Duration up to60s; each moving window is preflighted, not a250ms click cap. */
int elden_rtp_stream_start(EldenRtpStream *,EldenAwChip *[2],const EldenRtpIo *,const EldenRtpLimits *,const EldenRtpClip *);
/* Bounded fair FIFO refill; almost-empty is NOT drain/completion proof. */
int elden_rtp_stream_step(EldenRtpStream *);
int elden_rtp_stream_stop(EldenRtpStream *);
#ifdef __cplusplus
}
#endif
#endif
