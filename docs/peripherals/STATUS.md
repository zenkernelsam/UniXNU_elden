# Offline peripherals — current status

Branch `offline-peripheral-fixes`, from main@f82da007 on 2026-11-16.
Resolve the current revision from the branch ref. Accepted offline deliveries include the
RTC-003/004 guarded owner facade, the Haptics-003/004 compiled-recipe RTP path,
the deterministic Apple recipe manifest builder, the NT36536 worker route,
the AW86937 output-safety regressions, and current motion/camera refusal
regressions, independent of mainline Wi-Fi and the completed GPU branch. The
full five-part Goal is **active**.

GPU is the first completed offline peripheral delivery and remains isolated on
`codex/elden-gpu-offline`. Its accepted A830/SM8750 source, build and merge
boundaries are documented in that checkout's `docs/GPU-GOAL-STATUS.md` and
`docs/GPU-MERGE.md`; it is not merged into this peripheral branch and is not a
hardware-qualified or flashable image.

## Current delivery and remaining work

| Objective | Source-built offline implementation | Required evidence still missing |
| --- | --- | --- |
| AP501U pen | Stock NT36536/F1 report decoder, independent Pencil HID/tilt hook, original touch delivery and reversible release, atomic named-provider owner facade, generation-tagged native bridge, real-worker NativeRoute (derived worker's own 109-byte packet/`Context` frame owner/timestamp/sequence) packaged through the scalar source-build/full-link edge audit | Actual raw pen/scan mode, physical AP501U answer through the route, Pencil type/force/tilt, palm/rotation/release, Notes/PencilKit and resume |
| Dual AW86937/iOS effects | Source-bound Apple recipe/AHAP compiler, deterministic source-manifest generator, bounded calibrated-gain catalog/RTP/budget, continuous 24 kHz dual-motor chunks through the serialized Broker/RTP worker; output remains disabled without a measured dual-motor profile | Mechanical/electrical gain/boost/frequency/duty/thermal/dual-load calibration; production recipe bank/profile bind, system capabilities/VirtualAudio/tunings/real UIKit requests |
| RTC/wake | Original PMK8550 read-onlyRTC/SPMI shared gate, board-clock registration, guarded named-owner persistence/alarm facade with stop/retire callback fences, original-worker reversible pen/touch pause/gesture IRQ/firmware resume | Real named NVMEM/SDAM provider, PMK8550 alarm IRQ owner, actual shared display/systemPM power provider, physical doubletap/sleep/wake cycles |
| Accel/gyro/rotation | Exact sensor protocols/OS17 HID, one original ADSP/GLINK/QRTR/FastRPC/SSC worker, retained native memory/cache/pack/listener and prepared-memory adoption, atomic whole-operation J317 orientation consumer packaged inert in the SSC full-link | Genuine same-device PD/registry/calibration/mount/unit/time/SUID/sample input; actual J317 SPU/CoreMotion/UI provider and rotation acceptance |
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
read/write uncertainty, unbound-provider refusals, stop-during-callback
publication fences, and
owner-IRQ-only alarm acknowledgement, no I/O after stop during owner/epoch,
retire refusal during active callbacks, and rebind after retire. The WSL
LLVM-14 suite passes 125 owner checks plus 308 concurrency checks and scalar
ARM64 objects are qualified on both toolchains. Stop gates the facade, while the RTC core lifecycle remains with
its clock owner. No physical NVMEM cell, SDAM offset,
alarm IRQ, RTC persistence, or wake cycle is present, and `Native.cpp` still
binds no provider.

The RTC owner facade is now guarded by separate atomic operation and provider
claims. Restore/learn/alarm/status/bind/retire lifecycles cannot overlap core
state or provider publication; stop during provider callbacks or a second bind
aborts later I/O and publication. The current source passes 125 owner checks
and 308 deterministic concurrency checks under ASan/UBSan and TSan on WSL and
CiscoMac, with warning-free scalar ARM64 objects. This remains host/model
evidence only; no NVMEM cell, alarm IRQ owner or Native.cpp binding exists.

RTC persistence and alarm `Owner.o` now ships in the offline RTC archive.
CiscoMac linked it with 11 owner exports, 30 original execution regions and
the existing clock registration and recovery order. The audit rejected an
old receipt paired with the new image. Physical providers remain unbound.

The AP501U owner facade now uses a single nonblocking atomic claim across
sampling, provider/send callbacks, delivery mutation, stop, retire and status.
Generation-tagged deferred stop cannot affect a successor binding; bind
refuses generation wrap and status/released refuse or conservatively report
during contention. WSL and CiscoMac pass 217 facade checks and 317 deterministic
pthread checks under ASan/UBSan and TSan, plus decoder/HID/pressure/tilt
regressions and scalar ARM64 objects. The provider binds only when the
`elden-pen=1` route arms; Pencil/hardware acceptance is false.

The default-off `drivers/EldenPen/NativeBridge.{c,h}` routes worker hooks
through that owner facade. Its generation-tagged gate and nonblocking
lifecycle claim close delayed-start races across stop/retire/rebind.
The bridge adds 86 unit and 179 lifecycle/race checks, including WSL/CiscoMac
TSan and scalar ARM64 objects. `drivers/EldenPen/NativeRoute.{c,h}` now binds
the real derived NT36536 worker as the named `EldenNvt36536` provider: each
worker push carries the actual109-byte packet, the worker's `Context` frame
owner, timestamp and sequence, and the facade re-fences provider owner/epoch
and frame owner/epoch per packet. `elden-pen=1` remains the only opt-in. The
route adds125 unit and51 deterministic race checks on both toolchains.
The pen build hash-qualifies and packages `Owner.o`/`NativeBridge.o`/
`NativeRoute.o`, and the full-link audit requires their 36 exports while
proving the direct worker→native→route→bridge→owner→decode/feed call chain
and materialized provider callbacks on the final image. Mac source build
preserved all 30 original execution regions and the HID/touch ABI. These are
source-build facts: no AP501U has answered, so `native_bound:false`,
`pencil_accepted:false`, `hardware:false`, `boot_packaged:false` all stand.

The orientation consumer now holds one nonblocking atomic claim across each
sample, provider callback, classifier update and rotation sink publication;
begin/stop/status serialize against the same lifecycle. Generation-tagged
deferred stops abort publication at callback fences and cannot affect a fresh
generation. WSL and CiscoMac pass 204 orientation checks plus 92 deterministic
pthread checks under ASan/UBSan and TSan, with scalar ARM64 objects clean. The
J317 provider and UI route remain unbound.

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
