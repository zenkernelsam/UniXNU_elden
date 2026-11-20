# Real frame snapshots and iPadOS17 camera provider

This offline delivery connects the actual existing Gen4 DMA/CCI/CAMSS frame
completion path to retained cached snapshots and an OS17 read-only IOKit
service. Two signed CMIO daemons consume those genuine snapshots and convert
packed RAW10 into a software BGRA/sRGB preview for the normal iPadOS17 external
camera discovery path. No test pattern, pixel generator, sensor gain write or
GPU is used. Hardware capture, calibration and camera-app acceptance remain
unverified; the original physical boot/power/SMMU owner is still required.

Source branch is `offline-peripheral-fixes` over commit9b10889. Read
`drivers/EldenCamera/frames-validation.json` for exact source/artifact hashes.
The five-part peripheral Goal remains active. No tablet access, boot package,
job/cache/trust installation, mainline edit or automatic merge occurred.

## Producer and consumer lifetime

`FrameOutput.cpp` requires the original `DmaBinding` capture to be prepared and
actually started, the current native pipeline status, the real DMA view and
the exact expected Gen4 sensor mode. It obtains2–4 cached kernel-user-shared
IOKit snapshot descriptors per sensor. Original IOBuffer bytes/prepare exports
are called directly; the captured getBytesNoCopy slot43 is never used.
Descriptors and partial allocation state remain retained until reset.

The original camera executor calls `elden_camera_output_pump` after its actual
interrupt/service wake. It executes the real pipeline service/dequeue,
device-written visibility and DMA CPU-borrow operation, copies only that held
input, explicitly returns it, then requeues. Returned input and requeue are
separate: a requeue failure cannot fabricate that the CPU still owns a buffer
it already returned. Failed release retains the input and allows cleanup to
retry. Original Linux frame sequence0 is valid, followed by strictly increasing
sequences; zero timestamp, stale/malformed ownership or failed completion refuse.

Consumer `acquire` selects the newest completed snapshot and assigns its exact
ticket; held slots are never overwritten. Fully held output causes a drop and
actual input return/requeue, not reuse of a consumer-held buffer. `release`
requires the exact live ticket; stale/duplicate returns refuse. Output stop
requires original Board/pipeline/DMA/context retirement. Consumer-held output
stays held until explicit release even after physical camera stop. No memory is
freed or reassigned to another process speculatively.

`EldenCameraService` publishes only a real prepared snapshot producer and
actual mode info. The original owner must explicitly publish it; there is no
automatic initializer or startup caller. OS17 IOService/IOUserClient method
tables and metaclass inheritance are checked in the linked image. Metaclasses
are registered once by an explicit late call. The provider grants only the
named service access; TCC/AVFoundation camera permission remains its actual OS
boundary. One administrator client per sensor perboot avoids reassignment of
still-mapped snapshots to a different process. Close withdraws output admission;
no close automatically powers hardware or acknowledges physical shutdown.

The scalar interface in `ServiceAbi.h` exposes Info8words, Acquire12words and
Release1ticket. Memorytype0–3 is read-only and available only for the currently
held ticket. No raw MMIO, hardware gain, writable DMA alias, shared control page
or arbitrary pointer crosses that interface. `CameraClient.c` validates all
metadata, maps cached snapshot READONLY, converts, unmaps before exact ticket
return. Unmap failure closes the one process-owned service instead of claiming
consumer completion; original storage is retained until reset.

## Software preview and CMIO

`Raw10.c` uses all10packed bits. It centre-crops and samples whole Bayer cells,
applies explicit black/white and bounded software white-balance Q16, converts
linear values through the standard sRGB transfer, then rotates/mirrors into
BGRA. Geometry/stride/alias/overflow checks precede all writes. The expected
source Bayer/rotation is rearGRBG90/frontRGGB270, not a live calibration claim.
The default preview uses black0/white1023/unityWB and is explicitly uncalibrated;
it does not borrow T970's pedestal or claim hardware ISP/tuned colours.

The standard CMIO daemon methodology is pinned to the same OS17/21H461
`rui12366B/UniXNU-gts7xlwifi` commit88b34f97. Its license/provenance are retained
in `LICENSE.CMIO` and `frames-source.json`. No SM8250 register, carve-out,
firmware or sensor assumption is copied. Independent front/rear identities are
`org.unixnu.elden.camera.front` and `.rear`, both process name`eldencamerad`.
Each daemon refuses to publish a CMIO device until actual native camera
readiness is obtained. Existing Apple CMIO discovery and sample delivery handle
the ordinary external camera path; native built-in/Camera.app switching still
requires separate OS17 acceptance.

Output is720×960 BGRA with IOSurface-backed CoreVideo buffers. The real capture
timestamp is passed to CMSampleBuffer/CMIO. Missing frames/faults yield errors,
never artificial timestamps/pixels. Stop joins the existing capture thread;
full hardware stop remains the original owner/recovery task. Job templates
carry the normal OS17 `com.apple.cmio.registerassistantservice.system-extensions.matching`
event and per-sensor Mach identity; they are not added to the live launchd
cache. Mainline must use its reviewed exact-CDHash RAM overlay/cache/signature/
trust workflow before installation. TCC is unchanged; no Apple identity is
borrowed and no global launch constraint is disabled.

## Accepted offline evidence

Mac root `/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes/`
`lab/camera-frames-20261121`: source11/host13/native11/kernel11; stack10 and
provider9 are compiled from byte-equivalent accepted method sources. A prior
source9/11 comparison records the added host-only test include/provenance;
no changed native method is substituted silently.

- RAW10 independent pack/linear/sRGB/cell/rotation/mirror/calibration tests:
  1069cases6062checks. Pixel inputs are explicitly synthetic test fixtures.
- Four actual snapshot allocation/held-slot/release/requeue/retire scenarios,
  actual service22checks and five user ticket/map/unmap/conversion failure
  scenarios execute the same compiled methods under ASan/UBSan.
- Actual original DMA→Board→WL→sensorCCI/CAMSS→cachedsnapshot/acquire/release
  front/rear compositions pass: normal30192/drain30358/visibility30027checks.
  Original IOKit/PAR/power/GIC/MMIO/physical completion peers remain modeled.
- Two scalar OS17 kernel objects0warnings/noauto-init, full kernel link and
 11final shared/camera/runtime/sleep/motion/haptics/RTC/QRTR/native ABI/archive/
  startup audits pass. Existing source objects/archives, original recovery5500ms,
  30Apple executable ranges and original4K single provider are preserved.
- Both CMIO executables compile and verify ad-hoc4K signatures. Jobs/cache/trust
  are not installed; real sensor samples/frames and AVFoundation acceptance have
  not been observed.

Current kernel:
`e6459d9c77d17ec7d48ee9b3cdccc144a02f3a8baedaed68399f4cd5e3e60b28`.
Current native:
`70e4c098e5958fd51148b6683c6ce2971244e3bb0f0f61d7545f1689ae5dcae3`.
`frames-validation.json` is the candidate authority. Tools: `prepare_camera_frames.py`,
`test_camera_frames.py`, `test_camera_frame_stack.py`, `build_camera_frames.py`,
`build_camera_provider.py`, `link_camera_frames.py`, `audit_camera_frames_link.py`.
Required originalOS/header/native-parent paths are recorded in their outputs;
this is a source-built component/full-link delivery, not clean-mainline/boot
or hardware acceptance.

Next: real shared physical owner startup/PLL/RPMh/CSIrails/DMA and RAW10,
calibration and robust OS17 service lifecycle/Camera.app integration. Mainline
owns physical testing; no guessed ready provider should be installed. Pen
Pencil acceptance, motion PD/calibration/mount/time/samples/rotation UI, actual
screenPM wake, measured haptics/system effects and RTC retention/alarm remain
inside the active five-part Goal.
