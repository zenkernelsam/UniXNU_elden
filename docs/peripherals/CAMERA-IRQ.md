# Camera IRQ delivery on the original executor

The offline camera pipeline now supports the original sleepable camera
executor waiting for actual CSID interrupts. The existing sensor, CCI, CAMSS,
DMA visibility, CPU-borrow, snapshot, explicit return and requeue methods are
used unchanged. It adds no background thread or automatic hardware startup.
Physical power/PLL/RPMh/rails/SMMU admission is still supplied by the real owner.

The fixed ArmadaOS TB322FC/SM8750 source is Linux `6f97250e`. Its CAMSS DTS
declares rising-edge interrupts; the native attach retains that exact type.
The source CSIPHY lane setup disables its peripheral IRQ masks, and its Gen4
VFE ISR is empty. Only the original CSID frame line is enabled. The original
`csid_isr` reads/clears TOP, buffer-done and RDI status, applies SOF/EOF order,
then drives the original VFE queue callbacks. No new interrupt-status bit or
register value is guessed for this delivery.

## Original executor integration

After actual Board/DMA capture has started and `elden_camera_output_open`
has succeeded, the same original executor calls:

```c
int rc = elden_camera_output_enable_irq(output);
/* Continue only after the actual return code is zero. */
/* In the existing original executor's capture loop: */
rc = elden_camera_output_pump_irq(output, 250);
```

The first call binds the actual `current_thread()` and retains it until reset.
It requests one initial service pass without inventing an IRQ or frame. The
second waits on the retained CSID latch and performs the normal pump after an
event. `-11` means there is no frame or all output slots are held. Other errors
must return to the real stop/recovery owner. The public OS17 camera client
still receives read-only snapshots, never MMIO/DMA aliases or IRQ controls.
Mainline supplies the outer executor loop and its own stop orchestration.

The primary IRQ only masks its owned line, updates atomic latches/generation
and wakes the retained executor. It takes no mutex, allocates no memory,
sleeps nowhere and calls no physical/DMA provider. A callback during attach is
also masked and latched; the start refuses instead of dropping that event.

The executor drains actual GIC active/inflight state before original CSID
service. It rearms only after those source records are acknowledged. It never
clears GIC pending during service/rearm: an edge after source acknowledgement
must remain deliverable. Inflight/pending/generation checks cover an IRQ that
remasks the line before the unmask readback. Wait enrollment is followed only
by atomic peeks and clear/block, closing arrival-before-sleep without invoking
a sleepable provider while the wait is queued. No output lock spans the wait.

Stop first withdraws IRQ/frame admission and wakes the original waiter. It
masks/drains owned GIC active/inflight state, runs the original sensor/CSI/VFE/
DMA stop, and checks pending/retirement before resources can be released.
Failed drain retains every registration, worker reference, mapping and CPU
buffer lifetime to reset. Consumer-held snapshots still require explicit
ticket return even after physical camera retirement.

## Outer lifecycle composition

`drivers/EldenCamera/CaptureSession.{h,c}` composes the explicit outer
lifecycle the same original executor performs. It is deliberately not linked
into the audited native objects: `audit_camera_frames_link.py` rejects any
new outer caller of the output open/publish/withdraw/IRQ entry points, so the
composition stays a source-level contract plus host regression. No new
thread, automatic boot/start or register route is added; every step invokes
the existing board/DMA/output/service owner exactly once.

Per target, `elden_camera_capture_session_begin` runs the required order:
board prepare → sensor identity probe → real initial DMA queue → stream
start → retained snapshot open → original CSID listen. A failed step latches
session error and teardown stays with the board's existing partial-state
cleanup. `elden_camera_capture_session_publish` publishes the running
producer through the real service; its failure leaves capture running.
`elden_camera_capture_session_serve` is the executor's bounded-wait entry
(the retained `pump_irq` timeout path, `-11` on timeout or no frame).

`elden_camera_output_close` is the distinct pre-teardown admission close: it
sets the output's real stopping flag so new pump/service/client admission is
refused before physical teardown, while consumer-held tickets stay
returnable and retirement still requires `output_stop` after the actual DMA/
pipeline retirement. `elden_camera_capture_session_stop` runs that close
then the real physical drain/stop, and reaches the board stop even when no
prepare ever completed so a stop before begin or a failed board_prepare
leaves honest terminal state. `elden_camera_capture_session_shutdown` first
attempts admission close on every output; a refused close on a fully opened
output gates all physical teardown (the consumer path can still be live),
while a lockless partial output from a failed `output_open` is already
admission-impossible, stays explicit (`output[sensor]` retained while
`opened[sensor]` is clear), is never claimed freed, and blocks neither
sensor. With both admissions closed it then attempts both physical stops
even when the first drain fails, reporting the first real error and
retaining state. The board shutdown, whole-arena retirement and per-output
retirement follow only after both stops settle. A published slot is retired
through `elden_camera_service_withdraw_output`, which the owner calls only
when `elden_camera_service_output` proves the registry holds exactly this
session's output — that same path retires a partially published service
(slot committed, start failed) and a foreign registration is never touched
through the sensor-only lookup; `elden_camera_service_withdraw` itself
stays uncalled by this composition. `EldenCameraClient::clientClose`/`free`
likewise perform only admission withdrawal plus the client's own ticket
return; physical teardown and output retirement stay with the serialized
session owner.

Host regression `drivers/EldenCamera/TestCaptureSession.cpp` drives the real
Board/DMA/output/service methods over the same modeled OS peers as the frame
stack test: `normal` (success plus held-ticket return after retirement),
`partial-start` (rear stream denial; the shared board error honestly refuses
the surviving front capture and cleanup still completes),
`partial-prepare` (acquire authority denial leaves the board clean and the
surviving sensor serving), `partial-open` (lock-allocation failure retains
an explicit lockless partial output that blocks nothing), `publish-fail`
(failed publish leaves the committed slot, a permanent duplicate refusal and
a running capture), `client-close` (client close returns only its own ticket
and withdraws admission; the output retires later at owner teardown),
`irq-timeout`, `drain-fail` (first-sensor drain failure still closes both
outputs and attempts the peer stop; retry completes), `close-gate` (a
refused close on a fully opened output gates all physical teardown; retry
completes) and `early-stop` (stop before begin marks the sensor terminal
and a later begin refuses).

The production-object path is a separate claim from this source-only
composition: `tools/prepare_camera_session.py` +
`tools/build_camera_session.py` compile `CaptureSession.c` alone to a
scalar arm64 object against the prepared actual headers, and
`tools/audit_camera_session_link.py` audits a session-linked image by
requiring each mandatory owner method's direct/tail calls to every required
leaf (begin/serve/publish/stop/shutdown edges plus the `clientClose`/`free`
client-ownership edges), by checking leaf symbols resolve to their expected
defining objects, and by bounding the retained parent link command to the
approved camera replacements — `NativePipeline.o`, `FrameOutput.o` and now
`Service.o` as a third source-built replacement since the client-ownership
methods changed — plus exactly one new `CaptureSession.o`. The session-aware
link itself is `tools/link_camera_session.py`: it preserves the parent link
command and recovery/Apple30 gates, replaces exactly those three receipt-bound
objects, and inserts exactly one `CaptureSession.o` (bound to the session build
receipt; an already-linked session object or a missing build profile rejects).
Rebuilt-but-unchanged peers (`CameraPipeline.o`, `CaptureBinding.o`) must hash
byte-identical to the retained inputs — an undeclared delta is never linked.
`tools/test_camera_session_link.py` carries focused negatives for an undeclared
object delta, a duplicate session object and a changed retained input; `tools/
test_camera_session_audit.py` keeps the required-edge negatives. The session
link and full audit ran on the qualified Mac inputs over the original OS17
kernelcache `f18b4df8…`: `link_camera_session.py` produced a session-linked
`kernel.macho` `8b8e9eb4…` / `native.macho` `8e8f9bb3…` off parent kernel5
(`d7fc72c7…`) with the three receipt-bound replacements plus one
`CaptureSession.o`, all other inputs byte-identical, recovery bound 5500ms and
all 30 original executable regions preserved; `audit_camera_session_link.py`
passed with 16 leaf APIs edge-verified, the six session entries caller-free and
no added `__mod_init_func`. This is still an offline link — it does not prove
physical capture, and the candidate keeps the parent's not-yet-loadable flags.

## Offline evidence and limits

Current exact source/artifact authority is `drivers/EldenCamera/irq-validation.json`.
Accepted Mac `lab/camera-irq-20261121`: source5/frames5/native5/frames-native5,
stack5/bridge5/kernel5 and input-gates9. The accepted IRQ audit is irq-audit10;
the eleven other final-audits5 results pass. Their original failed IRQ audit
and all preliminary build/compare attempts stay in the private archive.

- Original CAMSS core: 218 scenarios, 1,202,627 checks.
- Eighteen actual native cases cover original polling, front/rear IRQ delivery,
  wrong owner/thread, timeout, arrival before/during sleep, stop during wait,
  rearm race/failure, attach race, mask failure and active IRQ stop refusal.
- Actual original IRQ→worker→DMA→snapshot front/rear composition passes
  normal63174/drain63382/visibility61846 checks. Each normal sensor produces
  four modeled source completions. Both consumer-held slots cause a real input
  return/requeue; the held ticket survives physical retirement until release.
- Actual snapshot→native service→CameraClient→RAW10 front/rear paths each pass
  1,382,433 checks; the input pixels remain explicitly synthetic fixtures.
- Twenty strict object/source identity/equivalence gates pass. Exactly
  NativePipeline.o and FrameOutput.o are replaced. All other link arguments
  remain unchanged. Service.o stays the parent object after comparing loaded
  code/data, local/global symbols and relocations to the source rebuild.
- Scalar OS17 objects, full kernel link, original recovery5500ms/Apple30/
  shared SMMU/pen/wake/motion/haptics/RTC/QRTR audits pass. No automatic
  executor, boot package, job/cache/trust installation or hardware trial.

IOKit, MMIO/GIC/IRQ scheduling, power, PAR/cache and physical capture peers are
modeled; these tests do not prove real RAW10 pixels, DMA retirement or camera
app use. Real shared physical startup, a sustained physical stream,
calibration/ISP and OS17 CMIO/AVFoundation lifecycle remain required. The pen,
calibrated motor effects, RTC/wake, motion/rotation and both-camera Goal stays active.
