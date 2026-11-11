// SPDX-License-Identifier: BSD-2-Clause-Patent
// Single-slot completion policy. Port supplies ordered MMIO, invalidate-only
// descriptor reads, a monotonic deadline and bounded pacing. No resubmission.
enum class UfsTransferEnd { Completed, Fatal, Timeout };
struct UfsTransferPoll {
  uint32_t polls = 0, doorbellSeen = 0, interruptSeen = 0, ocs = 15;
  bool rechecked = false;
};
static constexpr uint32_t kUfsFatalStatus = 0x71880U;

template <class Port>
static UfsTransferEnd pollUfsTransfer(Port &port, UfsTransferPoll &state)
{
  for (;;) {
    ++state.polls;
    uint32_t doorbell = port.doorbell();
    state.doorbellSeen |= doorbell;
    state.interruptSeen |= port.interrupt();
    if (state.interruptSeen & kUfsFatalStatus) return UfsTransferEnd::Fatal;
    if (!(doorbell & 1)) {
      state.ocs = port.completedOcs();
      // A zero doorbell alone does not prove that this command was accepted.
      // Also allow a completion so fast that software never observes DB=1.
      if (state.ocs != 15) return UfsTransferEnd::Completed;
    }
    if (port.expired()) {
      // A thread can be descheduled after its first MMIO sample. Before
      // quarantining, make one fresh ordered completion observation. This
      // remains the active command; no submission or buffer reuse occurs here.
      state.rechecked = true;
      ++state.polls;
      doorbell = port.doorbell();
      state.doorbellSeen |= doorbell;
      state.interruptSeen |= port.interrupt();
      if (state.interruptSeen & kUfsFatalStatus) return UfsTransferEnd::Fatal;
      if (!(doorbell & 1)) {
        state.ocs = port.completedOcs();
        if (state.ocs != 15) return UfsTransferEnd::Completed;
      }
      return UfsTransferEnd::Timeout;
    }
    port.delay();
  }
}
