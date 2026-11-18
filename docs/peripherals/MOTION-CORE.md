# Motion protocol and rotation math — offline core

The integer QMI/SSC decoder, discovery/request encoder and gravity-axis classifier
compile for ARM64/iOS17. Actual Gen4 transport, firmware attributes/calibration,
timestamp synchronization, IOHID/CoreMotion/UI and hardware samples remain to
connect. This core has no device access or synthetic producer.

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

## Validation and remaining work

Source2/host2 passes2622678 wire/math checks under ASan/UBSan, including32769
representable signed float values, full-message truncation, malformed nested
containers, identity/client/mount/units/bounds and rotation hold/clock behavior.
One warning-free scalar native object builds. Test samples are synthetic fixtures,
not a production sensor source or real hardware result.
Evidence: `drivers/EldenMotion/validation.json` and workbench
`lab/records/motion-core-20261119`; object in corresponding `edl_work`.

```sh
python3 tools/prepare_motion_core.py --output /new/source
python3 tools/test_motion_core.py --source /new/source \
  --llvm /path/to/llvm/bin --output /new/host-native
```

Next establish the exact Gen4 SSC/GLINK/FastRPC path and retained mainline owner,
observe firmware SUID/attributes/sample status and timestamps, qualify physical
axes and units, then bind actual IOHID/CoreMotion/rotation. No APPS IMU identity
or SM8250 SLPI hardware is guessed. No native transport/HID/full kernel/boot or
tablet test occurs in this delivery; full five-part Goal remains active.
