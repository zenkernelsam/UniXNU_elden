# Elden Offline Peripheral Acceptance Matrix

This matrix is the handoff boundary for the five-part offline goal. Host
tests establish source, ABI, lifecycle and refusal contracts. They do not
establish a Y700 device provider or user-facing acceptance.

| Objective | Current source entry | Host evidence | Required Y700 evidence | Current state |
| --- | --- | --- | --- | --- |
| AP501U pen | `drivers/EldenPen/NativeRoute.{c,h}`, `EldenPenHid.cpp`, `TouchSource.py` | NT36536 worker route, HID, tilt, hover, stale release, stop/pause/resume, scalar full-link | Raw AP501U packets, scan-mode behavior, Pencil type/pressure/tilt, palm and edge behavior, Notes/PencilKit, suspend/resume | Source path ready, physical provider absent |
| Dual AW86937 effects | `drivers/EldenHaptics/`, `tools/build_wave_manifest.py` | AHAP/wave compiler, bounded dual RTP, confirmed FIFO consumption, chip gain backstops and stop/thermal fences | Per-motor gain/boost/frequency/duty, dual-load current, thermal limits, measured carrier/LUT, system route | Output intentionally disabled without measured profile |
| RTC and wake | `drivers/EldenRtc/`, `drivers/EldenWake/`, `drivers/EldenNvt36536/` | PMK8550 read/carry, owner/alarm fences, GPIO/PDC/GIC route, async power readiness, pause/restore | NVMEM/SDAM provider, alarm IRQ identity, panel/system-PM provider, persistence and sleep/wake cycles | Provider boundary ready, physical owner absent |
| Accel/gyro rotation | `drivers/EldenMotion/` | SSC/QRTR/RPC/memory ownership, timestamp/SUID/mount validation, orientation generation fences | Same-device ADSP samples, registry/PD, calibration, mount and units, J317/CoreMotion/UI rotation | Consumer inert until real samples and provider |
| Front/rear cameras | `drivers/EldenCamera/`, `tools/link_camera_session.py` | CSID IRQ, DMA/SMMU, session lifecycle, RAW10 interface and CMIO boundary, front/rear refusal paths | Sensor power/PLL/RPMh/CSI rails, real IRQ/DMA, RAW10 frames, calibration/ISP, CMIO/app acceptance | Source-linked model only, no physical frames |

## Shared acceptance gates

1. Rebuild against the current mainline owner set. Do not overlay an old whole
   kernel or replace Wi-Fi, recovery, UFS, floor `1738713600@0`, or parent `3`.
2. Preserve the default-off and no-automatic-start properties until the named
   provider, owner/epoch, memory and power contracts are present.
3. Capture raw device evidence before changing calibration, gain, mount,
   timing, SUID, RTC epoch, pixels or waveform limits.
4. Run the component host suites and the component link/audit tools again after
   every mainline port. A green host suite is not a hardware result.
5. Set a component's hardware flag only after the required Y700 evidence in
   this matrix exists. The five-part goal remains active until all rows pass.

The authoritative narrative and current commit are in `STATUS.md` and
`MAINLINE-HANDOFF-PROMPT.md`.
