/* SPDX-License-Identifier: BSD-2-Clause-Patent */
EldenWakeFlow wake_flow={};EldenTouchWakePower wake_power={};EldenNvtImage wake_image={};
IOLock *wake_lock=nullptr;
void *wake_thread=nullptr; // owning original worker, avoids synchronous self-ack deadlock
uint32_t wake_enabled=0,wake_ready=0,wake_paused=0,wake_closed=0,wake_sequence=0,wake_ack=0;
uint32_t wake_asleep=0,wake_gesture=0,wake_busy=0;int wake_error=0,wake_result=0;
EldenTouchWakeStatus wake_status={}; // published under wake_lock; flow stays worker-only
