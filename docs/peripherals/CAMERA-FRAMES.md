# Real frame snapshots and iPadOS17 camera provider

This offline delivery connects the actual existing Gen4 DMA/CCI/CAMSS frame
completion path to retained cached snapshots and an OS17 read-only IOKit
service. Two signed CMIO daemons consume those genuine snapshots and convert
packed RAW10 into a software BGRA/sRGB preview for the normal iPadOS17 external
camera discovery path. No test pattern, pixel generator, sensor gain write or
GPU is used. Hardware capture, calibration and camera-app acceptance remain
unverified; the original physical boot/power/SMMU owner is still required.

Source branch is `offline-peripheral-fixes`, current implementation `c15d94e`
over `f3329c9`. Read `drivers/EldenCamera/bridge-validation.json` for current
source/artifact hashes. `frames-validation.json` retains the earlier delivery.
The five-part peripheral Goal remains active. No tablet access, boot package,
job/cache/trust installation, mainline edit or automatic merge occurred.

## Producer and consumer lifetime

`FrameOutput.cpp` requires the original `DmaBinding` capture to be prepared and
actually started, the current native pipeline status, the real DMA view and
the exact expected Gen4 sensor mode. It obtains2–4 cached kernel-user-shared
IOKit snapshot descriptors per sensor. Original IOBuffer bytes/prepare exports
are called directly; the captured getBytesNoCopy slot43 is never used.
Descriptors and partial allocation state remain retained until reset.

The original camera executor must explicitly call `elden_camera_output_pump`.
The current integration is bounded polling; a continuous IRQ/wake caller is
not installed. The pump executes the real pipeline service/dequeue,
device-written visibility and DMA CPU-borrow operation, copies only that held
input, explicitly returns it, then requeues. Returned input and requeue are
separate: a requeue failure cannot fabricate that the CPU still owns a buffer
it already returned. Failed release retains the input and allows cleanup to
retry. Original Linux frame sequence0 is valid, followed by strictly increasing
sequences; zero timestamp, stale/malformed ownership or failed completion refuse.

Consumer `acquire` selects the newest completed snapshot and assigns its exact
ticket. It discards only older READY snapshots so they cannot appear after
the newest frame is returned; already-held slots are never overwritten. Fully held output causes a drop and
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
Release1ticket. Its Bayer field uses `EldenRaw*` values, explicitly translated
from `EldenCameraMode`: source GRBG=0/RGGB=1, RAW10 RGGB=0/GRBG=1. Forwarding the
source integer causes the actual client to reject both cameras. Info and
Acquire now use the same boundary translation. Memorytype0–3 is read-only and available only for the currently
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
`lab/camera-bridge-20261121`: source1/host7/regressions3/native3/provider3/
kernel3/stack4/final-audits3. All source, commands, binaries and earlier failed
attempts are in the source-bound private archive, with 644 members rehashed
after Windows transfer. Historical `f3329c9` evidence stays separately archived.

- Actual snapshot methods→native Info/Acquire/Release/read-only map→actual
  CameraClient→RAW10 converter front/rear tests each pass 1,382,433 checks.
  Synthetic pixels are independently encoded in the sensor's Bayer sites;
  the tests verify colours, rotation, tickets, unmap-before-return and latest
  sequence. Physical acquisition and IOKit peers are modeled.
- Exact original `f3329c9` methods reproduce both client-open refusals and
  stale-frame delivery after the newest return. The old fixture is placed
  beside the old method include to ensure C++ quoted-include resolution really
  executes those original bytes. Preliminary failed comparisons are retained;
  only host7 is accepted as the final reproduction.
- RAW10 tests: 1069 cases/6062 checks. Five actual snapshot allocation/lifetime/
  release/requeue/latest scenarios, service22 checks and five actual user
  ticket/map/unmap/conversion cases pass under ASan/UBSan.
- Original DMA→Board→WL→sensorCCI/CAMSS→snapshot composition passes:
  normal30192/drain30358/visibility30027. The physical/cache/PAR/GIC/MMIO peers
  remain modeled; there are no real camera pixels or hardware retirement proof.
- Two scalar OS17 objects have zero warnings and explicit late metaclasses.
  Full kernel link and all 11 component/ABI/archive/recovery/startup audits pass.
  Declared inherited archive changes are checked; recovery5500ms, Apple30 and
  the original4K single owner remain preserved. Both CMIO providers compile and
  verify ad-hoc4K signatures, without installation or an AVFoundation claim.

Current kernel:
`c12fcecef16090d4110a2a9ca14ffc83c412fce87c72c803ccd456943dcd7669`.
Current native:
`2afd524cf92cab332cf6717b792f2a9296fb44d426a2c29ffeaad0f2f408db08`.
Private archive `camera-bridge-offline.tar.gz`: 25,579,962 bytes, SHA256
`cd8e9422a351ea88da7e99438fcae8775af0ff669def914f72f8c7be8aec7e0e`.
`bridge-validation.json` is current candidate authority. Tools:
`prepare_camera_frames.py`, `test_camera_bridge.py` (use
`--compare-original f3329c9` for the source regression), `test_camera_frames.py`,
`test_camera_frame_stack.py`, `build_camera_frames.py`,
`build_camera_provider.py`, `link_camera_frames.py`, `audit_camera_frames_link.py`.
Original OS/header/native-parent paths are recorded in outputs. This delivery
is not a clean-mainline boot or hardware acceptance.

## Next integration step

The original NativePipeline registers and masks its three IRQs; its current
callback treats an arriving IRQ as an error. Continuous frame delivery needs
the exact SM8750 DTS interrupt types and original `csid_isr` read/clear/done
logic routed through the existing admitted owner/executor. The IRQ path must
stay nonsleeping and actual stop/inflight/GIC/DMA retirement remains required.
This is an identified gap, not a newly installed service thread.

Real shared physical startup/PLL/RPMh/CSIrails/DMA, RAW10, calibration/ISP and
robust CMIO/Camera.app lifecycle still require mainline hardware evidence.
Pen force/tilt/palm, genuine motion PD/calibration/time/samples/UI rotation,
display/systemPM wake, measured motor effects and RTC retention/alarm remain
within the active five-part Goal. No guessed ready provider is installed.
