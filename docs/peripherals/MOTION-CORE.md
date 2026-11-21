# Motion protocol and rotation math — offline core

The integer QMI/SSC decoder, discovery/request encoder and gravity-axis classifier
compile for ARM64/iOS17. The subsequent actual OS17 accel/gyro HID/native
component is in [MOTION-HID.md](MOTION-HID.md). Actual Gen4 transport, firmware
attributes/calibration, timestamp synchronization, CoreMotion/UI and hardware
samples remain to connect. This core has no device access or synthetic producer.

## Reference and implemented path

The source-bound reference is gts7xlwifi88b34f97's same-OS17 `T970Ssc.c/h`.
Only QMI/SSC/protobuf and integer IEEE-754 conversion are adapted. QRTR and the
SM8250 SLPI register/runtime code are excluded; mainline remains owner of shared
transport. The donor README says actual accelerometer streaming is still next
and uses a fake-gravity test for UI rotation. That fake path is not copied here.
Neither the donor hardware nor a filename called sensors identifies Y700's IMU.

`CorePublic.inc` bounds full QMI/TLV messages and client/transaction identity,
encodes exact SUID discovery/config/disable requests, accepts only unambiguous
default accel/gyro SUID announcements, and extracts complete three-axis samples.
The entire nested message is validated before any result is copied. Unknown
SUIDs are filtered; wrong clients, malformed/duplicate/truncated known fields,
NaN/infinity, out-of-range samples and nonmonotonic batch stamps are rejected.
Failed output is preserved. No absent/failing provider is treated as ready.

Explicit hardening of the source reader rejects64-bit varint overflow, missing
or duplicate SUID/event fields, bad known-field wire types, packed-float tails,
extra float/SUID counts and duplicate/invalid status. The original parser's
fourth-float and packed-tail acceptance was reproduced separately and rejected
by the fixed public path. Unknown protobuf fields remain skippable when valid.

Units/range/SUID and the actual board mount must come from firmware attributes
and qualified Gen4 evidence. The API validates signed permutation matrices with
determinant+1; this algebra is not a physical calibration. Acceleration in m/s²
can be converted to g using rational9.80665; gyro remains rad/s Q16. No Apple HID
or UIKit unit claim follows from this physical-unit core.

Rotation recognizes a plausible gravity vector/dominant signed device axis after
a bounded hold. Flat/poor-quality input resets the pending candidate; duplicate
timestamps are stale and backwards time latches refusal. It emits ±X/±Y axes,
not an assumed UIKit orientation/polarity. SSC's19.2MHz timestamp convention is
used for relative holds; native synchronization/retention remains unverified.

`Orientation.c` is the guarded J317 orientation-consumer boundary over that
classifier — a source-side contract a mainline J317 SPU/CoreMotion/UI owner can
bind to, not a producer. It synthesizes nothing: an explicit `EldenMotionProvider`
must describe both accel and gyro (SUID identity, units/ranges and the real
signed-permutation board mount), report owner/epoch, map source ticks to
monotonic mach time and yield calibrated samples; the owner supplies the rotation
sink. `begin` qualifies every callback output before arming a generation;
`sample` re-fences owner/epoch and generation, rejects foreign SUID/unit/kind,
duplicate/reversed ticks, nonmonotonic mach mappings, out-of-range axes and
flat/poor-quality input, applies the real mount and m/s²→g normalization, and
only then feeds the existing classifier. The source ticks keep the source-side
monotonicity fence; the classifier's hold is driven by the mapped mach time
converted into its 19.2 MHz tick domain with the same ceiling rounding as the
SSC hold, so the hold duration tracks real elapsed time, not the source rate —
a zero, overflowing or nonmonotonic conversion latches a clock refusal. A
matured dominant axis reaches the sink; a stop landing inside any callback, a
failing sink and any call after stop roll back or refuse — nothing publishes
after stop. The published value stays the ±1/±2 device axis, never a UIKit
orientation enum or an 0x20/0x8a orientation HID node. No provider leaves the
consumer disabled by default.

## Validation and remaining work

Source2/host2 passes2622678 wire/math checks under ASan/UBSan, including32769
representable signed float values, full-message truncation, malformed nested
containers, identity/client/mount/units/bounds and rotation hold/clock behavior.
One warning-free scalar native object builds. Test samples are synthetic fixtures,
not a production sensor source or real hardware result.
The consumer boundary passes164 host checks over the real classifier —
provider-backed ±Y/−X publishes, real-mount remap, m/s² normalization, a
mach-driven hold (source-rate mismatch and conversion boundary/overflow) and
every fence/rollback path — plus one warning-free scalar `Orientation.o` that
imports only `elden_motion_rotation`. Its samples are host-only synthetic
fixtures.
Evidence: `drivers/EldenMotion/validation.json` and workbench
`lab/records/motion-core-20261119`; object in corresponding `edl_work`.

```sh
python3 tools/prepare_motion_core.py --output /new/source
python3 tools/test_motion_core.py --source /new/source \
  --llvm /path/to/llvm/bin --output /new/host-native
python3 tools/test_motion_orientation.py \
  --llvm /path/to/llvm/bin --output /new/orientation
```

Next establish the exact Gen4 SSC/GLINK/FastRPC path and retained mainline owner,
observe firmware SUID/attributes/sample status and timestamps, qualify physical
axes and units, then bind actual IOHID/CoreMotion/rotation. No APPS IMU identity
or SM8250 SLPI hardware is guessed. Subsequent native HID/full component link
exists in MOTION-HID.md; native transport/UI/hardware still remain absent.
No tablet test occurs; full five-part Goal remains active.
