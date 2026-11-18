/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ELDEN_WAKE_GESTURE_H
#define ELDEN_WAKE_GESTURE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {EldenWakeNoGesture=0,EldenWakeDoubleTap=1,EldenWakeArgument=-22,EldenWakeIntegrity=-84};
typedef struct {uint8_t raw_id,id;uint16_t linux_key;} EldenWakeGesture;
/* Fixed original NT36536: packet[1]>>3; ID30/function1 ->packet[3];
 * original13-entry12..24 table maps ID15 to KEY_WAKEUP143. Other gestures
 * produce KEY_POWER in Android but are deliberately not accepted as double
 * taps. Caller only invokes this on its owned, suspended gesture path; it must
 * never feed the same packet into normal finger/pen delivery. Bytes1..65 have
 * the original envelope checksum; this is not a checksum over pen bytes66+.
 * Disabled/unknown/bad packets leave the output unchanged. */
int elden_wake_gesture(const uint8_t *,size_t,int enabled,EldenWakeGesture *);
#ifdef __cplusplus
}
#endif
#endif
