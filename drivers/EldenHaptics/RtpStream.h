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
typedef struct {
    const int8_t *pcm[2];size_t frames;uint32_t rate_hz;uint8_t mask,gain[2];uint64_t id;
    /* Optional bounded chunk pull source. When source!=NULL, pcm[] must be NULL
     * and the worker pulls each bounded window instead of reading a resident
     * clip — one live session spans the whole clip.frames while payload memory
     * stays bounded. pull(motor,first_frame,out,want) fills up to 'want' frames
     * (<=64) for that motor at absolute source position first_frame, returning
     * the staged count (>0), EldenAwBusy while it must wait for the sibling
     * motor, or <0 on producer failure (stops the stream). first_frame must be
     * the exact next unconsumed position: a skipped/gapped or replayed frame
     * is a producer refusal, never a playable chunk. peak_squares is the
     * producer's verified worst moving-window cost reserved once up front.
     *
     * A pull only STAGES bytes into the worker's feed[] staging; it is not a
     * consume. consumed(motor,first_frame,count) is invoked by step() strictly
     * after elden_aw_rtp_feed() confirms those exact frames reached the FIFO —
     * the producer retires its source only on confirmed writes, so staged bytes
     * surviving an EldenAwBusy retry are never counted consumed and a cancel or
     * stop before the write drops them rather than false-completing. A pull
     * source without consumed is malformed. */
    void *source;
    int (*pull)(void *source,unsigned motor,uint32_t first_frame,int8_t *out,size_t want);
    int (*consumed)(void *source,unsigned motor,uint32_t first_frame,size_t count);
    uint64_t peak_squares[2];
} EldenRtpClip;
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
    int8_t feed[2][64];size_t staged[2];
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
