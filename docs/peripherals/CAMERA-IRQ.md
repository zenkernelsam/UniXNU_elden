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
