# Offline peripheral fixes — active

Branch: `offline-peripheral-fixes`, created from committed `main` at
`f82da007b624e32f2090d232896c3b9f4bd0a56e` on 2026-11-16. This is independent of
`codex/elden-gpu-offline` and mainline's active Wi-Fi work. No tablet access,
automatic merge, boot installation or alteration of shared mainline files.

## Scope and order

| Order | Deliverable | First offline acceptance | Hardware acceptance remains separate |
| --- | --- | --- | --- |
| 1 | AP501U pen | NT36536 pen decoder, independent report state and Pencil HID adapter, signed UIKit probe; retain finger/release behavior | Raw SPI pen frames, scan mode, actual Pencil type/force/tilt, palm/rotation/release |
| 2 | Dual AW86937 haptics | Owned GENI I2C/driver and bounded transport; source-bound Apple effect importer/converter, gain/energy/duration constraints; custom app then system route | Chip/firmware/readback, mechanical calibration, amplitude/boost/temperature, dual-load budget, actual UIKit requests |
| 3 | RTC and wake | Audit current board-clock registration, real provider and monotonic/calendar distinction; touch/screen wake lifecycle contracts | Actual RTC retention, wake source, resume, repeated suspend, existing watchdog/USB return |
| 4 | Accelerometer/gyroscope | Identify exact OEM sensor and transport, timestamp/unit/mount transforms; rotation classification and OS delivery | Real axes/calibration, orientation/lock/hysteresis, reliable CoreMotion/UI delivery |
| 5 | Front/rear camera | Exact sensor/board identities, CCI/CSI/CAMSS/ISP/clock/DMA ownership and OS17 service contract; implement source-supported pieces | Sensor response, calibration/ISP, real frames, buffers/privacy/stop/power; DT labels alone prove none of these |

The goal includes all five; completing a parser or feasibility note cannot close
it. Each component records source identity, implemented paths, host/native/link
checks and remaining hardware facts separately. No success stubs for missing
providers, guessed camera identities, fake RTC readiness or fabricated sensor data.

## Reference policy

Primary: ArmadaOS-Freedom/armadaos-freedom, specifically
`devices/lenovo-tb322fc/README.md`, its pinned SM8750 base, Elden DTS and applicable
patches. Secondary: Linux mainline; iPadOS interfaces use mature UniXNU and exact
OS17 donors. TB321FU/SM8650 and TB323FU/SM8850 are not board register/firmware
substitutes. Shared protocol ideas still require exact Gen4 source comparison.
Gaming focus is not independent proof of performance or stability; retain actual
source provenance and measurements. Reference updates never silently replace a
qualified candidate's frozen inputs.

Initial selected-ref review: workbench
`lab/reference-reviews/20261116-offline-peripherals/review.json`. Reference
checkouts and edits remain unchanged. Armada's current README explicitly says
camera control nodes only, functionality unverified. Front/rear camera work must
therefore include the missing pipeline rather than merely enabling those nodes.

## Haptics limits

Apple effect timing/envelopes and our motor's electrical/mechanical drive are
different quantities. Import local owned Apple assets with recorded hashes, not
redistributed raw binaries. Convert only decoded formats; unknown encodings are
errors. AHAP intensity/sharpness are not voltage or AW86937 gain. Do not replay a
Taptic Engine PCM stream or scale peak values and claim calibrated equivalence.

Separate semantic/transient mapping from continuous RTP. Preserve original
AW86937 stop behavior, firmware bounds/readback and per-channel ownership. Require
explicit hardware-qualified limits for gain/boost, frequency, duration/duty,
combined motor budget and thermal state before live output. Uncalibrated output
remains disabled; Android gain64 click is an observed test point, not a universal
safe maximum. Only hardware measurements can qualify conversion gains. Physical
speakers stay outside the haptics-only route.

## Mainline coordination and files

- Mainline remains sole owner of tablet tests and active Wi-Fi/PCIe/WCN/QRTR work.
- Reuse shared power/bus/IRQ providers; do not add competing controllers or change
  mainline's current stop order without a reviewed integration patch.
- Do not commit this work in Patch or on the GPU branch. Code/tools/configs go on
  this authoritative source branch; raw/private assets and evidence go under the
  workbench lab/edl_work. research contains Git clones only; one-off scripts use Temp.
- Native Mac builds must use a separate checkout/output after confirming no
  maintenance marker. No modification of another task's worktree or artifacts.
- Component status and precise tested commits live in this file and per-component
  notes. Merge-ready requires explicit evidence, not just presence in Git.
- Preserve source and logs; retirements use the requested recycle/trash process.

Mainline prompt: [MAINLINE-HANDOFF-PROMPT.md](MAINLINE-HANDOFF-PROMPT.md).

## Current status

Latest SSC native milestone on2026-11-19 is [MOTION-SSC.md](MOTION-SSC.md):
service400 discovery/strict attributes/per-sensor demand and a real native
worker connect to the existing OS17 HID.26manager18372 +6native614 checks,
five scalar objects and full kernel1640a2d3/native1041abbe pass. Real shared
QRTR/board/PD registry providers remain absent; no samples or UI rotation/hardware
acceptance. Recovery now stops motion before haptics/RTC/UFS. The latest
combined kernel retains input-sleep/camera/pen/Actuator/RTC and Apple30.

Latest Actuator milestone on2026-11-19 is in
[HAPTICS-ACTUATOR.md](HAPTICS-ACTUATOR.md): signed OS17 HAL and real type1
client/ring/original-worker connection, full offline kernel6c644610/native4a674e31.
13 native lifecycle scenarios/140 checks and135 HAL interface checks pass;
actual IOBuffer direct-call ABI defect fixed and old wrong-slot image rejected.
Calibration/catalog/profile remain absent/output disabled. Product capabilities,
tunings/VirtualAudio, calibrated hardware and all five-part acceptance remain open.
Private22802522B/663bad26 archive264members verified; workbench
`lab/records/haptics-actuator-20261119` has the current receipt. The following
component rows include earlier independently accepted milestones:

| Component | Implemented and checked offline | Next missing implementation/acceptance |
| --- | --- | --- |
| AP501U | Independent109B decoder and HID; actual OEM F1/envelope instruction qualification; same-report polar tilt;932 core,15 wire/310 scoped-hook checks;14641 tilt pairs; six scalar objects and full link/207-slot inheritance/10 overrides | Raw XNU frames/scan/integrity beyond envelope; real UIKit pressure/polar axes, Notes/PencilKit/palm/release/sleep; current mainline integration |
| Dual haptics | Exact Gen4 AW/GENI/queue/native worker; Apple compiler, RAM/RTP broker/shared budgets and signed48k HAL/type1 ring;16scalar objects/full link,168-service/186-client ABI, actual original IOBuffer calls;13native lifecycle scenarios/135HAL checks | Measured per-motor waveform/gain/thermal/bandwidth, product capabilities/tunings/VirtualAudio and actual iOS playback; current mainline/hardware/sleep acceptance |
| RTC | Real PMK8550 read/carry via observer-only SPMI/shared gate; volatile system-learned offset; upstream board-clock registration/get/set added to existing platform; five scalar objects/full link; actual initializer/callback/recovery audit | Persistent offset/epoch identity, wake source/alarm and sleep lifecycle; current-mainline/hardware retention |
| Wake | Exact OEM GPIO/PDC route and serialized flow now hooked into original worker;reversible pen/finger release,real Protocol restore/event wait/stop;12coordinator534checks and full link | Current-mainline port, actual display/system power provider and physical PDC/gesture/sleep/pen/finger cycles; no fake power readiness |
| Motion | Strict integer sample core/OS17 HID plus actual service400 discovery/strict attributes/session/demand/nativeworker;26manager18372+6native614checks/five new scalarobjects/full link,207/10 retained | Install real shared QRTR/Gen4 sensor-PD registry/calibration and board provider; qualify actual samples/mount/time; CoreMotion/UI and complete J317 orientation/SPU contract |
| Cameras | Exact Gen4 sensor/core plus CCIv2 read/write bodies, actual native CCI0/CCI1 mapping/owned IRQ drain and sensor binding;135CCI/11native scenarios with full sensor path,three new scalarobjects/full link | Actual shared power/CAMCC/GDSC/MCLK/pin provider, CAMSS/CSI/VFE/DMA/SMMU/IRQ/buffers/OS17 service, sensor identity/calibration/frames |

Earlier haptics-native combined haptics+RTC+pen kernel SHA256 is
`4889eee6af10d10d816f9f8e79704385d264a05c0590cc54cfccec080c21c72c`;
native `3ae88218356370f41d34f6ca5d29c63a5d41d36a948543fbab191029e74df845`.
All30 original Apple execution regions and pen inheritance/late initialization
remain checked. The RTC constructor only registers exact callbacks. The actual
recovery now calls haptics stop, RTC stop, then UFS stop. These are offline component
checks over fixed source-built v152 inputs; global prelink ABI/loadable/runtime/
relocation flags remain false. No boot candidate has been installed or tested.

Evidence: workbench `lab/records/offline-peripherals-20261119`. One delivery tar
under `edl_work/offline-peripherals-20261119` is24,340,607B, SHA256
`8ea11374b2f1c3e0dcb421b40f02a59e5382133cbdbf20104d1b3e5f46e9b1aa`;
all264 member hashes were verified. It contains no raw Apple assets or motor
firmware. Native Mac workspace remains
`/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes`; all these jobs
are terminal. Earlier unsuccessful attempts and their logs remain archived.

Component entries: [PEN-NATIVE.md](PEN-NATIVE.md), [HAPTICS.md](HAPTICS.md),
[RTC-WAKE.md](RTC-WAKE.md), and the source-bound validation JSON in each driver.
Latest haptics native delivery: `edl_work/haptics-native-20261119`,22,492,147B/
SHA256 `8f5edc573e62b8207c73fb582a6195da76deb447b5e072f91375cbe7377a830a`,
180 members verified. Standalone motor firmware is excluded; compiled native
embeds its verified bytes. Evidence: `lab/records/haptics-native-20261119`.
Sixteen native source inputs match the Git index. Accepted runs are source2/
native2/host2/kernel1; failed first attempts remain retained. The touch vote
writes now use the same guard, and either sibling's release preserves a live
wrapper1 peer. Details: [HAPTICS-NATIVE.md](HAPTICS-NATIVE.md).

Continue wake/motion/camera implementation and haptics continuous/system effects.
RTP/controller and explicit Apple compiler now have additional native/full-link
evidence in [HAPTICS-RTP.md](HAPTICS-RTP.md): up60s, measured bandwidth/rolling
budget provider gates and original142recipe/17AHAP tests with synthetic peers.
Native requests/catalog/shared budget now connect through the actual v2 broker;
see [HAPTICS-BROKER.md](HAPTICS-BROKER.md), kernel d64231c6 and actual handler/
budget/composed tests. Defaultcatalog0/no measured bank/outputdisabled. Subsequent
HAL/native Actuator source/full link now exists in HAPTICS-ACTUATOR.md; system
capability/preset/VirtualAudio and calibrated hardware acceptance still remain.
Motion and camera foundations now have code/native evidence in
[MOTION-CORE.md](MOTION-CORE.md) and [CAMERA-CORE.md](CAMERA-CORE.md). Motion now
also has [MOTION-HID.md](MOTION-HID.md), full linked kernelc89d6000 and actual
original constructor/dispatch/207-slot ABI checks. Neither has an actual Gen4
native producer/pipeline or hardware acceptance yet. All current
jobs are terminal; selected source inputs remain frozen after the68-root review.
Camera now has [CAMERA-CCI.md](CAMERA-CCI.md): source-bound exact CCI controllers/
queues/timing, signed-shift fix, native mapping/IRQ drain and core binding; full
kernel5c952842 and existing component audits pass. Actual power/receiver/frames/
OS17 camera and full five-part acceptance remain incomplete.
Latest original-worker input-sleep milestone is [INPUT-SLEEP.md](INPUT-SLEEP.md),
full linked kernelb448a408. Actual board power provider is absent/default-off;
no tablet, current-mainline merge or real sleep/wake acceptance occurred.
Wake protocol is recorded in [WAKE-PROTOCOL.md](WAKE-PROTOCOL.md); its command
delivery is separate from physical sleep. [WAKE-IRQ.md](WAKE-IRQ.md) now adds
owned native IRQ/flow and full link63486fdf over Actuator. Actual original-worker
and display/system PM connection remains absent; full Goal stays active.
The delivered haptics calibration is absent/output disabled; host synthetic
gains are not measured limits. Mainline still needs a reviewed source port to
its current touch/SPMI/recovery code. No tablet or automatic merge occurred.
