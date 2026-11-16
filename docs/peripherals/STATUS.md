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

AP501U foundation is implemented in `drivers/EldenPen`: independent109-byte
packet decoding and acknowledged desired/accepted state, ordered release retry,
stale-contact expiry and terminal stop/clock-fault behavior. Actual C tests pass
924 checks under ASan/UBSan; two ARM64/iOS17 scalar objects compile without
warnings, imports or initializers. Source commits891c065/86fb562 and
`drivers/EldenPen/validation.json` identify the tested inputs. Synthetic frames
are used; this is not a raw SPI capture or HID/UI acceptance. Finger source bytes
match basef82da007 unchanged.

The next layer now builds and fully links: separate OS17 Pencil HID publication,
owned pressure conversion, and pen sampling outside finger-change suppression.
Five native objects, original HID tables, linked207-slot inheritance/10exact
overrides,30original Apple execution regions and entry preparation passed.
The UIKit diagnostic is signed for iOS17 but uninstalled. Details and reproducible
commands: [PEN-NATIVE.md](PEN-NATIVE.md), `drivers/EldenPen/hid-validation.json`.

Next: qualify raw pen packet/F1/integrity and scan assumptions, implement UIKit
polar tilt conversion, then review current-mainline integration/boot and live
input. This link uses a fixed source-built v152 baseline, not the current Wi-Fi
candidate. Default stays off; no live-feature readiness claim. The other four
workstreams remain pending. All five requirements stay active; GPU stays separate.

Native workspace: `/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes`,
sharing Git objects but no mainline build outputs. Component run2 in
`lab/pen-core-20261116` is accepted; run1's checker mistook a hexadecimal address
for a vector register. The fixed checker parses instructions; no production code
changed for that correction. Both attempts are retained under workbench
`edl_work/pen-core-20261116`; summary is in `lab/records/pen-core-20261116`.
