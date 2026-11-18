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

Implementation through `b341697` is archived and verified on 2026-11-19:

| Component | Implemented and checked offline | Next missing implementation/acceptance |
| --- | --- | --- |
| AP501U | Independent109B decoder and HID; actual OEM F1/envelope instruction qualification; same-report polar tilt;932 core,15 wire/310 scoped-hook checks;14641 tilt pairs; six scalar objects and full link/207-slot inheritance/10 overrides | Raw XNU frames/scan/integrity beyond envelope; real UIKit pressure/polar axes, Notes/PencilKit/palm/release/sleep; current mainline integration |
| Dual haptics | Actual Gen4 AW/GENI/queue plus native SE6/worker/root-only user client/shared touch vote guard; full link/168-service +186-client ABI;66 platform/3755 checks,304168 touch,2362 mailbox/91341 composed; earlier Apple import/sample conversion retained | Continuous waveform compiler/RTP/system Actuator, measured per-motor calibration; current mainline/hardware/sleep acceptance |
| RTC | Real PMK8550 read/carry via observer-only SPMI/shared gate; volatile system-learned offset; upstream board-clock registration/get/set added to existing platform; five scalar objects/full link; actual initializer/callback/recovery audit | Persistent offset/epoch identity, wake source/alarm and sleep lifecycle; current-mainline/hardware retention |
| Wake | Exact OEM caller/ID15/KEY_WAKEUP143 and panel50:13/50:11/IRQ-wake oracle1024;3439 actual gesture and38 SPI/fault checks; two scalar native objects/one new protocol function | Actual owned GPIO162/PDC109 IRQ, normal worker/HID drain/resume and display/system sleep; no permanent polling substitute |
| Motion | OS17 SSC/protobuf/HID donor and Gen4 transport gap documented; no proven APPS IMU node | Actual OEM sensor/config/SSC transport, unit/mount transforms and CoreMotion/UI delivery |
| Cameras | Exact Gen4 S5KJNS rear/GC08A8 front control nodes and driver/CAMSS sources identified | Actual CCI/sensor/CSI/VFE/DMA and OS17 camera service; real sensor identity/calibration/frames |

Latest combined haptics+RTC+pen kernel SHA256 is
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
Wake protocol is now recorded in [WAKE-PROTOCOL.md](WAKE-PROTOCOL.md); its command
delivery is separate from the still-missing native IRQ/power/resume path.
The delivered haptics calibration is absent/output disabled; host synthetic
gains are not measured limits. Mainline still needs a reviewed source port to
its current touch/SPMI/recovery code. No tablet or automatic merge occurred.
