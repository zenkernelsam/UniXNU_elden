# Offline peripherals — current status

Branch `offline-peripheral-fixes`, from main@f82da007 on2026-11-16. Latest
source commit is `8c89b6b`; accepted offline deliveries also include the
RTC-003/004 guarded owner facade, the Haptics-003/004 compiled-recipe RTP path,
and the deterministic Apple recipe manifest builder, independent of mainline Wi-Fi and the completed
GPU branch. The full five-part Goal is **active**.

## Current delivery and remaining work

| Objective | Source-built offline implementation | Required evidence still missing |
| --- | --- | --- |
| AP501U pen | Stock NT36536/F1 report decoder, independent Pencil HID/tilt hook, original touch delivery and reversible release, guarded named-provider sampling facade with stale-owner lifts and confirmed-retire/rebind | Actual raw pen/scan mode, UIKit Pencil type/force/tilt, palm/rotation/release and resume, real NT36536 worker→facade binding |
| Dual AW86937/iOS effects | Source-bound Apple recipe/AHAP compiler, deterministic source-manifest generator, bounded calibrated-gain catalog/RTP/budget, continuous 24 kHz dual-motor chunks through the serialized Broker/RTP worker; output remains disabled without a measured dual-motor profile | Mechanical/electrical gain/boost/frequency/duty/thermal/dual-load calibration; production recipe bank/profile bind, system capabilities/VirtualAudio/tunings/real UIKit requests |
| RTC/wake | Original PMK8550 read-onlyRTC/SPMI shared gate, board-clock registration, guarded named-owner persistence/alarm facade with stop/retire callback fences, original-worker reversible pen/touch pause/gesture IRQ/firmware resume | Real named NVMEM/SDAM provider, PMK8550 alarm IRQ owner, actual shared display/systemPM power provider, physical doubletap/sleep/wake cycles |
| Accel/gyro/rotation | Exact sensor protocols/OS17 HID, one original ADSP/GLINK/QRTR/FastRPC/SSC worker, retained native memory/cache/pack/listener and prepared-memory adoption, guarded J317 orientation consumer over the existing gravity classifier with mapped-time hold and terminal provider-drift generation fence | Genuine same-device PD/registry/calibration/mount/unit/time/SUID/sample input; actual J317 SPU/CoreMotion/UI provider and rotation acceptance |
| Front/rear cameras | Exact Gen4 sensor/CCI/CAMSS/clock/WL power and original DMA queue/visibility; shared early SMMU; original-executor CSID IRQ/wait/service; explicit capture-session composition; receipt-bound 3-object + session-object full link and 16-leaf audit; signed OS17 CMIO/RAW10 preview | Actual original power/PLL/RPMh/CSIrails/early-owner startup, physical RAW10/IRQ/DMA/retirement, calibration/ISP, camera-app/service acceptance |

No new tablet access, flashing, QEMU, mainline merge or automatic boot/service
installation occurred. Host MMIO/IOKit/PAR/clock/GIC/firmware peers are modeled,
not hardware proof. Missing calibration is not replaced with a guessed safe
gain, sample, pixel, RTC epoch or callback success. Unqualified motor output
stays disabled. Completing a parser/component cannot close the full Goal.

The source-side haptics wave compiler is qualified by 4,895 host checks and
freestanding ARM64 object builds on WSL LLVM-14 and CiscoMac LLVM-20. Its
bounded chunks now also reach the same serialized native Broker/RTP worker
through a pull-source clip under one live session — recipe->chunk->FIFO flow is
qualified by 688,101 host checks on the real GENI/AW peer chain, and `fed`
accounting now waits for confirmed FIFO writes via the `consumed` ack (staged
bytes persist across busy retries; unconfirmed tails drop honestly on
cancel/stop).
`tools/build_wave_manifest.py` now emits a deterministic build-time
`EldenWaveSource` manifest from the original Apple sources plus a qualified
carrier/calibration bank — deduplicated static sample arrays, events,
automation and sorted nonzero untagged ids — and `tools/test_wave_manifest.py`
qualifies a generated manifest end-to-end through WaveCompiler/WaveFeed in
5,821 host checks on both toolchains. Synthetic
linear-LUT carriers are host-only; no measured calibration is present and no
production manifest/profile is bound, so no output route is enabled. The
guarded orientation consumer is qualified by 204 host checks and scalar
object builds on both toolchains. Owner/epoch drift terminates the generation,
clears the pending candidate and requires a new begin. Its hold uses
provider-mapped mach time converted to the existing classifier's
19.2 MHz tick domain. No real J317 provider, physical samples or UI route is
connected.

RTC-003/004 adds `EldenRtc/Owner.{c,h}`, a default-off named-provider and
owner/epoch fence in front of the existing RTC persistence and PMK8550 alarm
core. Host checks cover stale generations, callback re-entry, failed
read/write uncertainty, stop-during-callback publication fences, and
owner-IRQ-only alarm acknowledgement, no I/O after stop during owner/epoch,
retire refusal during active callbacks, and rebind after retire. The WSL
LLVM-14 suite passes 104 checks and scalar ARM64 objects are qualified on both
toolchains. Stop gates the facade, while the RTC core lifecycle remains with
its clock owner. No physical NVMEM cell, SDAM offset,
alarm IRQ, RTC persistence, or wake cycle is present, and `Native.cpp` still
binds no provider.

Wake-power-001/002 adds `EldenWake/PowerOwner.{c,h}`, a default-off guarded
display/system-PM owner contract between the real panel/rail provider and the
existing `EldenTouchWakePower` flow. It requires a named provider with a live
nonzero owner/epoch, queues wake work asynchronously, and accepts an explicit
panel/rail-ready result before invoking the existing bounded resume path.
Atomic nonblocking claims fence provider-thread readiness against touch-worker
callbacks, stop, retire, duplicate requests, stale generations and rebinds.
The host suite passes 118 owner checks and 575 deterministic concurrency checks
under ASan/UBSan and TSan on both toolchains; scalar ARM64 objects are clean.
No panel registers, rail values, readiness, physical provider or Native.cpp
binding is supplied, so hardware and production state remain false.

## Latest accepted candidate

[CAMERA-IRQ.md](CAMERA-IRQ.md), source commit `b85cacd`, carries the CSID IRQ,
frame service and explicit capture-session lifecycle over the retained owner.
The current full-link output uses receipt-bound `NativePipeline.o`,
`FrameOutput.o`, and `Service.o` replacements plus one `CaptureSession.o`.
`audit_camera_session_link.py` verifies all 16 required leaf APIs and six
caller-free session entries. The emitted kernel is `8b8e9eb4…`, native image
`8e8f9bb3…`; all 30 original executable regions, recovery5500ms, Apple30,
and no-automatic-start flags are preserved. Global loadable/runtime/relocation
flags remain false. `CAMERA-FRAMES.md` describes the OS17 snapshot/CMIO and
Bayer/latest-ticket boundaries.

Accepted original core218/1202627checks,18 native scenarios,20 strict input
gates; front/rear original IRQ→DMA→snapshot normal63174/drain63382/visibility61846;
actual service→CameraClient→RAW10 each1382433checks. Twelve final component/
IRQ/ABI/archive/recovery audits pass. IOKit/MMIO/GIC/power/PAR/cache/physical
completion and pixel test fixtures are modeled, not hardware capture evidence.

Parent IRQ image `d7fc72c7…` / native `fa0b40a8…` remains the qualified
camera-IRQ interface parent. Session-linked output `8b8e9eb4…` / native
`8e8f9bb3…` is the current source-build candidate. The production link is
offline evidence only: physical IOKit/MMIO/GIC/power/PAR/cache, actual camera
frames, and ISP/app acceptance remain unverified. Mainline must port/rebuild
against its current owners, then prove sustained physical RAW10/retirement and
app/ISP acceptance.

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
