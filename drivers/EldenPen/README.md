# Elden AP501U pen input

`PenReport.c` decodes the pen area of the same109-byte NT36536 buffer already
read by the touch worker. It preserves raw axes, pressure, signed tilt, distance,
buttons and unqualified battery/tail bytes. Normalized axes match the existing
0..32767 coordinate convention. Identity packets are metadata;0xFF releases;
unknown formats (including0xF1) and invalid fields leave the destination unchanged.

Field layout comes from the stock NT36536 module and Armada's shared protocol
patch, with identities/anchors in `source.json`. Android observation supplies the
default19040/30400/4095/±60 limits. Stock's alternate8191 pressure configuration
is accepted only when the caller explicitly supplies it; this does not change
firmware or select a new scanning mode.

Finger checksum bytes1..65 do **not** cover the pen. This decoder does not claim
pen CRC/integrity verification. Distance is raw, not calibrated height, and the
battery byte is not a validated percentage. No raw XNU SPI pen capture exists yet.
The fixture packets used by tests are synthetic, based on these source facts.

`Delivery.c` maintains separate desired/accepted pen state. The owner serializes
calls and supplies actual HID delivery results. A failed send leaves accepted
state intact; failed lift retries before any later press. Identity traffic does
not keep a stale contact alive. Invalid transport/decode uses `fault`, screen or
driver shutdown uses `stop`, and regular `tick` retries releases and expires stale
contacts. The caller supplies the monotonic time and explicit stale deadline;
the deadline is a policy parameter, not a measured hardware sampling guarantee.
Clock reversal latches refusal of new contacts while still allowing a lift.

There is no background thread, device access, HID sender or native runtime hook
in these components yet. The current finger decoder/worker/HID remain unchanged.
Upcoming work is the independent OS17 Pencil HID service and a hook outside the
finger-change condition, followed by a signed UIKit diagnostic and full native
link. A parser/native object does not prove UIKit Pencil or Notes operation.

Run `python3 tools/test_elden_pen.py --llvm /path/to/llvm/bin --output /new/path`
on macOS for actual C host tests with ASan/UBSan and scalar ARM64/iOS17 object
checks. It uses synthetic packets and an explicitly modeled send callback.
Native HID, full-kernel and hardware acceptance remain separate.

2026-11-16:924 host checks and both native objects passed; see `validation.json`.
Both objects have no undefined symbols or automatic initializers and contain no
FP/SIMD instructions. The initial checker incorrectly matched hex address d0 as
a vector register; parsing only instruction text fixed it without changing the
production sources. All original failed/successful logs remain in the workbench.
