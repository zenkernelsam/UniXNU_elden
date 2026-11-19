# OS17 Actuator route — offline implementation

The haptics-only HAL plugin, native type1 client, shared ring and original
SE6/AW worker are now connected in source and fully linked offline. The plugin
is signed for iOS17 but not installed. Calibration, catalog and Actuator profile
are absent in the delivered native image; output refuses. MobileGestalt,
VirtualAudio route settings, product tunings and real UIKit/CoreHaptics playback
remain unqualified. This milestone does not complete haptics or the five-part Goal.

## Actual path and ownership

The `EldenActuator.driver` factory creates one output-only device with UID
`Actuator`, one mono Float32/48kHz stream, no input and no default audio-device
eligibility. Exact same-OS17 `format` and `actuatorData` property bodies come
from Pagani drivers `c888efd3ce1b817d13f28783a77cf689cb2255c9`, originally ported
from Sheng. The donor's speaker hardware, voltages and VirtualAudio byte patches
are not imported. Its original BSD-2-Clause-Patent license is retained in source
and the signed bundle. Source generation verifies the original donor digest.

The plugin opens `EldenHapticsService` type1. Only an actual platform binary
named `audiomxd` or `mediaserverd` is admitted. The service's narrow
`NativeSandboxGrantProcess` uses the existing UniXNU sandbox-grant convention;
the corresponding mainline XNU patch must be present and its actual open tested.
Type0 remains administrator-only and keeps its v2 RAM/effect selectors0..5.
Type1 cannot submit those commands or caller-selected gain, boost or I2C data.

| Type1 operation | Shape | Meaning |
| --- | --- | --- |
| Selector6 | No input, eight output scalars | Magic/version/bytes/ticks/owner/epoch/state/generation |
| Selector7 | No input or output | Worker-acknowledged StartIO/new run |
| Selector8 | No input or output | Worker-acknowledged actual STOP |
| Memory type8 | One shared descriptor | Fresh session's397312B ring mapping |

The descriptor is prepared and retained to reset. A4096B header precedes16384
slots of24B each. Each slot binds sample-Q15, absolute48k frame and run generation
under a bounded sequence protocol. The kernel retains its own owner, epoch,
anchor, generation and cursor; shared fields do not select hardware addresses,
timebase or authorization. Old-run frames and a stale producer generation refuse.
The plugin rejects nonfinite/fractional/oversized writes and saturates Float32
to signed Q15. Per-CoreAudio-client StartIO/StopIO tracking prevents one client's
close from stopping another. IOKit matching notification discovers a native
service published after the audio process's Initialize; no periodic discovery
poll or phantom silent device substitutes for a missing provider.

Only the original owning worker claims the broker, reads chip state, initializes
the live bridge, acknowledges new generations and performs physical STOP.
Open publishes metadata and waits up to500ms for preparation. Commands are
serialized and acknowledged within500ms; timeout withdraws output and closes.
The worker snapshots command sequence/desired state under the publisher's lock
and uses its own acknowledged running flag. A partially published StartIO cannot
start stale samples. Repeated StartIO is idempotent. Phase3 publication fencing
prevents an allocation finishing after final shutdown from becoming active.

Every open receives a distinct ring/history and kernel owner. Up to eight
attempts are retained per boot; old mappings cannot become a new owner's ring.
Closing first invalidates producer admission, then the worker stops/releases.
Failed STOP, exceeded settlement, clock rollback or uncertain ownership retains
descriptor, chip state and budget until reset. There is no false release success.

## Waveform, timing and budgets

The bridge explicitly decimates48k to24k with a box2 filter, then runs the actual
signed-sample mapper with independent measured motor LUTs. This is an explicit
profile contract, not a model of Apple's Taptic Engine. Both chips are prepared
before GO; FIFO status/feed/standby/STOP restore use the existing exact AW86937
and GENI implementation. No DMA/GSI/reset ownership is added.

The broker reserves shared moving-window square/motor-on maxima before GO.
Those reservations coexist with RAM/catalog RTP history across reconnects and
never expire while active/held. Live history retains per-run square peaks and
motor-on accounting, including zero PCM and FIFO waits. Only actual confirmed
STOP can settle a reserved maximum down to conservative observed cost, retained
through real stop time plus window. Two-chip STOP/restore latency counts toward
physical motor-on limits. A too-small duty limit therefore holds the exceeded
reservation even when standby was observed. An actual-stop settlement cannot
revive an already-held entry.

`NativeInputs.actuator_header` requires the exact TB322FC/chips/firmware7V,
reviewed native max gains/combined/thermal/shared budget, two measured waveform
maps, decimation and full-path bandwidth evidence at the selected bus rate.
Dual24k requires at least60000 payload bytes/s under the explicit25% margin;
raw I2C clock is not bandwidth proof. Maximum run60s, bounded gaps/underrun and
startup timing are enforced. Battery temperature is an admission observation,
not independent motor temperature or a safety certificate.

The generated `ActuatorProfile.cpp` is a separate object that returns NULL in
the default image. This keeps the real native allocation/lifecycle/call graph
compiled without ever compiling a synthetic enabled profile into a candidate.
Both `elden-haptics=1` and `elden-actuator=1`, actual recovery/root readiness,
correct19.2MHz clock and measured profile are required for live admission.

## ABI defect found and fixed

Captured XNU headers place `IOBufferMemoryDescriptor::getBytesNoCopy()` at slot43,
which is `appendBytes` in the actual OS17 table. Even explicitly qualifying the
C++ virtual call still emitted that wrong dispatch in kext mode. The native
bridge now binds the exact original exported method directly by its symbol.
The remaining open virtual, `prepare(0)` at slot27, matches the original table.
`audit_haptics_link.py` checks the exact direct bytes call and prepare instruction
and rejects the old kernel3. It also requires actual profile, allocation,
authorization, live and broker call edges. Compilation alone did not reveal this
defect; the final original-vtable audit did.

## Accepted evidence

Final native source8/native9/lifecycle7/kernel4; plugin source8/plugin4:

-13 actual native lifecycle scenarios/140 checks,135 actual HAL interface/ring
  checks,4117 ring checks and285013 live/ring/LUT/GENI/AW checks pass ASan/UBSan.
  OS scheduling/allocation/clock/MMIO are explicit peers, not device observations.
-165 shared-budget,283066 broker and118 actual service/client-handler regressions
  pass; nine catalog and15 Actuator generator negatives pass. Positive generator
  data is clearly synthetic HOST-ONLY and is never used in native builds.
-16 native objects compile scalar:15 motor/profile objects without warnings,
  one retained touch aggregate warning. All430 consumed native dependencies are
  hash-checked; default profile/catalog/calibration are absent.
-Full kernel `6c644610dc4bfc303cea8f07e9c79eae354f39f2994f2951b4eaa86a965a40c7`,
  native `4a674e3171bf9ad03740fbbbd55cbd9f283a9d1781b4e1171970945a08ebfb10`.
  Apple30 regions, service168/client186 with6/8 exact overrides, pen207/10,
  RTC registration/callbacks and real haptics→RTC→UFS recovery order pass.
  Global ABI/loadable/runtime/relocation flags remain false.

Windows workbench evidence: `lab/records/haptics-actuator-20261119`.
Private archive: `edl_work/haptics-actuator-20261119/actuator-offline.tar.gz`,
22802522B, SHA256
`663bad2608e583d26e9b9ab8226bbf922b3e99a9c185d971a6a86fa082230e1a`.
All264 regular members are verified. Standalone firmware, synthetic positive
assets and compile-only SDK symlinks are excluded; compiled firmware remains in
the private native binary. Failed early builds/fixtures/ABI and first archive
attempt are retained separately, not promoted as accepted artifacts.

## Reproduction and mainline continuation

Use the isolated Mac checkout and the existing fixed pen/RTC/source-built v152
inputs. Every output path must be new; tools refuse overwriting accepted runs.
`prepare_actuator.py` prepares plugin/ring/live; `build_actuator_plugin.py` signs
only and never installs. `prepare_haptics_native.py` accepts original firmware,
pen source/build and optional reviewed calibration/catalog. Default omission
produces disabled output. `build_haptics_native.py` compiles16 objects;
`link_haptics_native.py` links them over the fixed RTC/pen image. Run the three
component link audits on the resulting exact kernel, not on a previous image.
Actual commands/source records/logs are retained in the verified private archive.

Mainline must port these changes to its current touch/SPMI/recovery code, preserve
Wi-Fi/GPU/QRTR work, then qualify calibrated dual-motor limits and actual type1
open before enabling product capability/tuning/VirtualAudio configuration.
Current model is historically J317/iPad8,1; do not copy J120 settings blindly,
advertise closed-loop capability merely from AW resonance, or create a duplicate
hapticd job. The remaining system route is described in
[HAPTICS-IPADOS-FEASIBILITY-20261114.md](../HAPTICS-IPADOS-FEASIBILITY-20261114.md).
Suspend/drain, actual CoreHaptics timing/underrun/thermal and real dual-load
acceptance remain required. Native wake/SSC/camera pipelines are still active work.
