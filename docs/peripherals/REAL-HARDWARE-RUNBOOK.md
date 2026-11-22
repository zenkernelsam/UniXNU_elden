# Y700 Real-Hardware Acceptance Runbook

This runbook applies after the current mainline has ported the offline source
against its own owners. It is a test order and evidence checklist. It is not
a boot image and it does not authorize flashing.

## Before every session

- Keep the stock recovery path, floor `1738713600@0`, parent `3`, UFS and Wi-Fi
  owners intact. Use a readback and hash receipt for every candidate.
- Keep the long USB data connection stable. Use the short port for charging
  only when the measured setup is known to be safe. Stop if either port,
  cable, connector or charger becomes warm or current changes unexpectedly.
- Start the PC UART/USB listener before entering the kernel. Save the complete
  boot, panic, provider and return-to-fastboot log with a timestamp.
- Confirm the candidate is default-off for pen, haptics, RTC persistence,
  wake-power, motion and camera. Enable one component per session.

## Component order

1. **RTC and wake**. Read the PMK counter and SPMI status. Verify the named
   NVMEM and alarm providers, owner/epoch and IRQ identity before any write.
   Use a short future alarm, then a suspend/wake cycle. Abort on counter
   rollback, unexpected register access, missing panel readiness or a wake
   without the matching generation.
2. **Motion**. Capture real ADSP/SSC registry, SUID, mount, units and timebase
   records before requesting samples. Validate static gravity and rotation
   with the display stationary. Abort on missing calibration, invalid time,
   foreign provider identity or fabricated samples.
3. **Pen**. Capture raw NT36536 packets with fingers idle, hover, tip down,
   lift, barrel button and simultaneous finger contact. Check the actual HID
   service and PencilKit/Notes behavior. Do not apply donor panel geometry or
   BLE pressure merging. Abort on unexpected scan mode, malformed tail bytes,
   stuck tip, missing release or a foreign worker context.
4. **Camera**. Test one sensor at a time. Prove real power/PLL/CCI/CSID/DMA
   readiness, one RAW10 frame, queue retirement and the next frame. Verify
   front/rear identity and CMIO ownership. Abort on synthetic pixels, missing
   IRQ retirement, stale tickets, SMMU route changes or automatic start.
5. **Haptics last**. Do not enable output until each AW86937 motor has a
   measured gain, boost, resonance, duty, current and thermal profile. Bind
   the dual profile, start at the lowest measured gain, and test one motor
   before dual load. Confirm STOP, standby and FIFO acknowledgement after
   every effect. Abort on any uncertain stop, over-current, thermal rise,
   busy retry that replays bytes, or unconfirmed FIFO tail.

## Evidence required for acceptance

Keep raw UART logs, register readbacks, provider identity/epoch, source and
image hashes, power/current measurements, and the user-visible result for
each component. A host test or linked object proves only its source contract.
Set a component hardware flag only after its row in
`GOAL-ACCEPTANCE-MATRIX.md` has all required Y700 evidence. The five-part Goal
remains active until every row passes.
