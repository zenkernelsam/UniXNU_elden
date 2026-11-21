# Offline peripherals — current status

Branch `offline-peripheral-fixes`, from main@f82da007 on2026-11-16. Current
implementation delivery is `c15d94e`, independent of mainline Wi-Fi and the
completed GPU branch. The full five-part Goal is **active**.

## Current delivery and remaining work

| Objective | Source-built offline implementation | Required evidence still missing |
| --- | --- | --- |
| AP501U pen | Stock NT36536/F1 report decoder, independent Pencil HID/tilt hook, original touch delivery and reversible release | Actual raw pen/scan mode, UIKit Pencil type/force/tilt, palm/rotation/release and resume |
| Dual AW86937/iOS effects | Source-bound Apple recipe/AHAP compiler, bounded calibrated-gain catalog/RTP/budget, native service and OS17 Actuator HAL/ring/original worker | Mechanical/electrical gain/boost/frequency/duty/thermal/dual-load calibration; system capabilities/VirtualAudio/tunings/real UIKit requests |
| RTC/wake | Original PMK8550 read-onlyRTC/SPMI shared gate, board-clock registration, original-worker reversible pen/touch pause/gesture IRQ/firmware resume | Persistent epoch/alarm, actual shared display/systemPM power provider, physical doubletap/sleep/wake cycles |
| Accel/gyro/rotation | Exact sensor protocols/OS17 HID, one original ADSP/GLINK/QRTR/FastRPC/SSC worker, retained native memory/cache/pack/listener and prepared-memory adoption | Genuine same-device PD/registry/calibration/mount/unit/time/SUID/sample input; complete J317SPU/CoreMotion/UI rotation |
| Front/rear cameras | Exact Gen4 sensor/CCI/CAMSS/clock/WL power and original DMA queue/visibility; shared early SMMU; real cached snapshot service; signed OS17CMIO/RAW10 preview | Actual original power/PLL/RPMh/CSIrails/early-owner startup, physical RAW10/IRQ/DMA/retirement, calibration/ISP, camera-app/service acceptance |

No new tablet access, flashing, QEMU, mainline merge or automatic boot/service
installation occurred. Host MMIO/IOKit/PAR/clock/GIC/firmware peers are modeled,
not hardware proof. Missing calibration is not replaced with a guessed safe
gain, sample, pixel, RTC epoch or callback success. Unqualified motor output
stays disabled. Completing a parser/component cannot close the full Goal.

## Latest accepted candidate

[CAMERA-FRAMES.md](CAMERA-FRAMES.md), code `c15d94e` over `f3329c9`, connects
original DMA→Board/CCI/CAMSS done/visibility→CPU borrow→cached snapshot→explicit
return/requeue to the OS17 read-only IOKit/CMIO consumer. Sensor Bayer enums
are explicitly translated into the RAW10 ABI. Acquiring the newest frame
discards older READY snapshots; held tickets retain their original lifetime.

Actual front/rear snapshot→native service→user client→RAW10 paths each pass
1,382,433 checks. Exact `f3329c9` methods reproduce both original client-open
refusals and stale delivery. RAW10 1069 cases/6062 checks; five snapshot
scenarios; original full stack normal30192/drain30358/visibility30027 and all
11 final ABI/archive/recovery audits pass. Physical and OS peers/pixels are modeled.

Kernel `c12fcecef16090d4110a2a9ca14ffc83c412fce87c72c803ccd456943dcd7669`;
native `2afd524cf92cab332cf6717b792f2a9296fb44d426a2c29ffeaad0f2f408db08`.
Two scalar OS17 objects have zero warnings and no automatic initialization.
Actual original archives and declared deltas, recovery5500ms and Apple30 are
audited. Signed providers/jobs/cache/trust are not installed. The original
pipeline remains bounded polling; continuous IRQ delivery is still required.
`bridge-validation.json` is current authority; `frames-validation.json` is
historical. The 25,579,962-byte private archive has 644 verified members.

Parent [SHARED-SMMU.md](SHARED-SMMU.md), code`d7571f2`, uses an atomic early
exact1401/1c00/all4ADSP-session batch and original4K algorithms once. Complete
context/TTBR/table graph is re-audited after actualroutewrites before publish.
173core1400032/11composed2395232 and all11audits pass. Kerneld054975d is the
retained parent. Original physical startup/security/touch/clock authority is
still mandatory; current-mainline Wi-Fi ownership must be reviewed before port.

## Integration order

1. Review current mainline's unique SMMU/SPMI/RPMh/clock/display/input/QRTR owners,
   real recovery and preserved floor1738713600@0/parent3. Port source and rebuild
   against current baseline; never overlay an old whole kernel onto mainline.
2. Install actual shared early authority before touch seals; prepare all selected
   memory/contexts, commit one exact immutable batch, activate retained memory,
   then allow original touch/SSC/camera starts. See SharedNative.h/Peripheral.h.
3. For motion, use original SSC/ADSP producer with genuine PD/calibration/mount/
   time input, then finish complete rotation consumer. For cameras, establish
   real shared rails/PLL/RPMh/CCI/CAMSS/DMA and original IRQ/service delivery,
   take RAW10 and retire correctly, then validate source-built OS17 snapshot/
   CMIO preview and actual app lifecycle.
4. Complete calibrated motor/system route and actual display/systemPM wake/RTC
   persistence using original owners and functional trials. Mainline owns tests.

## Sources, files and handoff

Primary board source: ArmadaOS-Freedom `devices/lenovo-tb322fc/README.md`, exact
EldenSM8750 DTS and applicable patches; frozen Linux6f97250e for reviewed inputs.
Next Linuxmainline; qualified same-OS17 UniXNU donors for OS interfaces. TB321FU/
SM8650 and TB323FU/SM8850 are not board register/firmware substitutes. Gaming
focus is not performance evidence. Reference sync never silently swaps candidates.

Code/config/build tools live here. Workbench `Patch/Y700-Elden`: research holds
independent Git clones only; lab/records holds compact evidence; edl_work holds
private artifacts; one-off scripts use WindowsTemp. Source commits must be on
this branch, not Patch/GPU/mainline. Preserve peer edits/staging and current
recovery/floor guards. Mac uses its isolated checkout and hash-checked inputs.

Windows paths: `C:\Users\Cisco He\.codex\worktrees\offline-peripheral-fixes\UniXNU_elden`;
workbench`D:\Desktop\Git\Patch\Y700-Elden`.
Mac`/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes`.
[INDEX.md](INDEX.md) maps component references; [MAINLINE-HANDOFF-PROMPT.md](MAINLINE-HANDOFF-PROMPT.md)
is the concise mainline prompt. [MAINTENANCE.md](MAINTENANCE.md) describes Trash
and artifact path receipts. [Historical status](../archive/peripherals-20261121/INDEX.md)
retains prior whole documents exactly. Current entries are rewritten when
evidence changes, not appended with every older contradictory milestone.
