# Owned GPIO/PDC wake path — offline component

The exact TB322FC GPIO162/PDC109 route, actual native IRQ adapter and serialized
wake flow are implemented and fully linked over the fixed Actuator/RTC/pen image.
They are not called by the current touch worker or display/system PM callbacks.
This is an offline component, not a demonstrated double-tap-to-wake feature.
The full five-part Goal and real sleep/wake acceptance remain incomplete.

## Exact board route

Armada's TB322FC README sources Linux commit
`6f97250ead155fc3b980856323e99a4623dae73d`. The raw Git files are hash-bound in
`drivers/EldenWake/irq-source.json`; `audit_wake_irq_board.py` checks their actual
table/range/type/version fields. GPIO162 maps to PDC pin109. The range
`<98 609 32>` yields GIC SPI620, hence absolute INTID652. Normal TLMM summary
INTID240 is a different route; a GPIO number is never a GIC interrupt number.

| Register fact | Exact source value |
| --- | --- |
| GPIO162 physical window | `0x0f1a2000`, input/GPIO already owned by touch |
| TLMM IRQ configuration/status offsets | `8` / `12` |
| TLMM application target shift | `8`, unlike Sheng's shift5 |
| Conditional wake present/enable bits | `6` / `7` |
| PDC physical base/version | `0x0b220000` / offset`0x1000` |
| PDC109 configuration | offset`0x2c4`, falling-edge type`2` |
| PDC>=3.2 enable | Same pin configuration bit3 |
| PDC<3.2 enable | Shared bank offset`0x1c`, bit13 |
| Owned GIC parent | INTID652, rising edge after PDC inversion |

The exact Linux direct-PDC policy keeps this line masked at TLMM and configures
the type at PDC. The adapter requires an already-owned GPIO input with TLMM
summary disabled; it changes no mux, drive, rails, clocks, SE or SMMU. An actual
PDC version/readback and exclusive ArmCpu attach are required before enable.
An attach refusal does not authorize masking a peer's existing652 registration.

Portable legacy enable-bank semantics preserve sibling bits and require a common
external serialization provider. The native adapter refuses PDC<3.2 before
attach/writes because no global legacy-bank provider is connected here. This
prevents claiming a read/modify/write race is safe from host tests alone.

## Actual native IRQ implementation

`NativeIrq.cpp` maps/retains the exact GPIO/PDC/GIC windows and delegates to the
same methods used by `TestNativeIrq.cpp`. It uses the existing real ArmCpu
exclusive attach/mask functions, checks Group1/enabled/edge/priority readback,
and checks actual GIC RWP and the owned enable bit. Drain waits up to2ms for
owned GIC active status and in-flight handler to clear, then writes only652's
pending-clear bit. Failure retains maps/registration and refuses readiness.

The actual IRQ handler does not perform SPI, allocation, HID, power requests or
logging. It masks its own652, latches one pending request and wakes the original
worker's event. The latch never wraps or turns repeated IRQs into replayed taps.
An immediate IRQ on unmask is accepted by its own observed pending event even
though the handler already masked the enable bit again. The registration and
all failed/partial maps are retained to reset. Caller-supplied owner/epoch/drain
admission is mandatory; no success fallback exists.

## Flow and remaining native connection

`Flow.c` must run serialized on the original touch worker. A PM caller must post
a bounded acknowledged command rather than running concurrent SPI itself.
Suspend requires actual worker/DMA pause, finger and pen release, owned IRQ
acquisition/disarm, then exact OEM`50:13` gesture or`50:11` deep command. Gesture
mode arms the real route. Command success proves delivery, not physical suspend.

On an IRQ, the owning worker takes/drains the pending latch and reads the actual
109B packet using existing `Protocol.c`. Only checksum-qualified gesture15
becomes a wake request. Other gestures rearm; errors retain paused resources.
An accepted wake request keeps sampling paused. Only the actual display/system
power-ready callback may resume, after real IRQ drain, latched-event retirement,
original firmware/transport/PID/HID restore and input release. Failed requests
or unavailable power never fabricate normal input readiness.

Mainline still needs to connect pause/resume to its latest actual touch worker,
nonterminal finger/pen release, actual display/rail/firmware callbacks and system
wake request. Existing terminal `elden_pen_native_stop()`/touch teardown cannot
serve as a reversible pause. The frozen GOP PM callback only schedules logical
scanout; it is not physical panel-power readiness. Do not bypass that gap with
fake ready callbacks, a guessed power-key event or permanent full-power polling.
Actual PDC hardware version/route ownership and gesture capture remain untested.

## Evidence and reproduction

Accepted Mac `lab/wake-irq-20261119/host-native6` and `kernel2`:

-50 actual route cases/1392 checks;12 actual native IRQ scenarios/16439 checks;
 1233 actual flow/Protocol/Gesture/route checks pass ASan/UBSan. MMIO/GIC,
 allocation/scheduling, power and HID remain explicit peers.
-Four scalar ARM64 objects compile without warnings/automatic initializers.
 Full kernel SHA256
 `63486fdf0f186b71049f2ac0526a4035927d36e713bbceafcc6f02d02920e624`,
 native `bbd93bc89770542147c19f8a7a67508d5c8b86ff93e95e8e8dec19ea85dc1e7d`.
-The wake call audit verifies actual internal IRQ/flow/gesture edges, unchanged
 initializer count and current-owner code, and no inherited startup/PM hook.
 Apple30 execution regions, Actuator168/186 ABI/direct IOBuffer call, pen207/10,
 RTC callbacks and haptics→RTC→UFS recovery are rechecked. Global prelink
 ABI/loadable/runtime/relocation flags stay false.

Run `tools/analysis/audit_wake_irq_board.py` against the fixed reference Git,
`tools/test_wake_irq.py` with the source-built v152 native capture and LLVM,
then `tools/link_wake_irq.py` over the Actuator kernel. Outputs must be new.
Run wake, haptics, pen and RTC audits on that exact resulting kernel. No tool
installs a boot image or accesses the tablet. Failed/older runs remain retained.

Workbench receipt: `lab/records/wake-irq-20261119`. Private archive:
`edl_work/wake-irq-20261119/wake-irq-offline.tar.gz`,19273807B, SHA256
`08e9be2d2248ac1e14f14a3628c19a3a6dd2cec2b7c8aa8cb0fcf3f9e4943dbc`,
all53 regular members verified. Code stays on `offline-peripheral-fixes`; notes
and raw evidence stay outside the Git-only research directory. No mainline/GPU
merge, tablet test or stock/floor change occurred.
