/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ELDEN_AW_RTP_H
#define ELDEN_AW_RTP_H
#include "AwChip.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenAwRtpRunning=1,EldenAwRtpDrained=2,EldenAwRtpStopped=3};
typedef struct {
    EldenAwChip *chip;uint32_t rate_hz,duration_us;
    uint8_t saved[7],changed,owned,held,started;
    uint64_t last_clock,deadline;
} EldenAwRtp;
typedef struct {uint8_t almost_full,almost_empty,state,faults,done;} EldenAwRtpStatus;
/* Caller zero-initializes once, retains original chip/bus/clock owner. Chip
 * identity/SRAM and max_gain must already be qualified. No dynamic boost
 * voltage or gain override is exposed. Exact stock theory-time map:
 * reg46&60: 00=24k,20=48k,40=12k. FIFO thresholds match base/2 and base-base/4.
 * FIFO AE means ALMOST empty, never completion. Return0 is setup/GO delivery,
 * not successful waveform playback. */
int elden_aw_rtp_start(EldenAwRtp *,EldenAwChip *,uint32_t rate_hz,uint8_t gain,uint32_t duration_us);
int elden_aw_rtp_prepare(EldenAwRtp *,EldenAwChip *,uint32_t rate_hz,uint8_t gain,uint32_t duration_us);
int elden_aw_rtp_go(EldenAwRtp *);
int elden_aw_rtp_status(EldenAwRtp *,EldenAwRtpStatus *);
/* ≤64 samples, no-increment port32, fresh AFS admission before every block. */
int elden_aw_rtp_feed(EldenAwRtp *,const int8_t *,size_t);
/* Actual STOP/standby plus original config restore/readback. Uncertainty holds
 * the original chip and bus; never overwrites/restores during live playback. */
int elden_aw_rtp_stop(EldenAwRtp *);
#ifdef __cplusplus
}
#endif
#endif
