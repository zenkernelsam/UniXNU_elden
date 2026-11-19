# OS17 accelerometer/gyroscope HID — offline component

The real native IOHIDDevice/event-driver adapter now compiles and fully links
with exact OS17 constructor/driver qualification. It requires an admitted Gen4
producer and emits no synthetic samples or static capability nodes. No actual
Y700 SSC transport, firmware attributes, sample stream or automatic screen
rotation is connected yet. The five-part Goal remains active.

## Source scope and actual path

The same-OS17 donor is Pagani drivers
`c888efd3ce1b817d13f28783a77cf689cb2255c9`. Only accel/gyro descriptors and
original IOHID event ABI facts are selected; the original BSD-2-Clause-Patent
license and unmodified descriptor source are retained. Its SM8750 phone's
board memory/RPC/QRTR/axis defaults and hardware are not adopted as Y700 facts.

| Physical input | HID identity | Original event |
| --- | --- | --- |
| Parsed acceleration, Q16 g | `0xff00/0x03`, Elden Accelerometer | Accelerometer13 |
| Parsed gyro, Q16 rad/s | `0xff00/0x09`, Elden Gyroscope | Gyro20 |

The two original60B descriptors contain Sensor-page report interval/latency
features and X/Y/Z input elements. Actual events are made by the original OS17
constructors and sent through original event-driver dispatch198. This is
independent of the pen/finger device and preserves their scoped hooks.
The complete original IOHIDFamily executable digest, constructor bodies and
220-slot event-driver table are checked before source generation. The class
inherits the already-qualified207-slot IOHIDDevice layout and exactly10 overrides.

`NativeHidMethods.inc` is included by both the native build and host tests.
Start requires actual owner/epoch admission for both kinds, two distinct valid
SUIDs, calibrated physical ranges, selected iOS sign flags, real19.2MHz timebase
and nonzero bounded max age. No NULL/failing demand/provider is accepted.
The existing parser applies its qualified physical mount/unit conversion first;
the HID boundary's explicit sign flags are a separate reviewed iOS convention.

The actual device publishes through IOHIDFamily with per-product matching
personalities. Its producer worker must call `elden_motion_native_hid_bind`
before sampling: this installs only that sensor driver's ReportInterval slot44
hook and forwards the original method. It also takes a real preexisting interval
request that may have arrived before the hook. Without this explicit bind, waiting
for the first sample to install the hook would create a circular startup wait.

Demand changes and sample dispatch run on the device workloop. Invalid interval,
narrowing, missing provider or failed producer demand refuses. If the original
property method rejects after a real demand change, the old demand is restored;
failed rollback closes public admission. Only the scoped driver gets the cloned
220-slot table; every other method and other HID driver remains original.

Samples require the correct kind/SUID/g-or-rad/s, finite parsed physical range,
bounded original timestamp age/future, monotonic accepted sequence and actual
driver binding. A bad/newly unavailable sample cannot overwrite accepted state.
Event construction failure keeps the prior accepted sequence/timestamp. Dispatch
means the original method was invoked; it is not CoreMotion or visible-rotation
acceptance. Stop closes admission and makes a real zero-rate demand for both
producers; a failed stop remains failed. Services/workloops/hooks are retained to
reset, and transport drain remains the original producer's separate obligation.

## Orientation and transport gaps

The donor warns that an orientation`0x20/0x8a` device without AppleSPULib's actual
user-client interface can cause CoreMotion to dereference a failed open. This
component publishes accel/gyro only. It creates no orientation/SPU stub, fake
gravity, MobileGestalt accel/gyro node, fallback timestamp or imaginary sensor.
Current historical J317 identity must be checked against actual consumer paths
before choosing a complete rotation interface. A phone's J120 behavior is not
an iPad model contract.

Actual Gen4 SSC/ADSP sensor PD, registry/calibration, GLINK/FastRPC/QRTR owner and
attributes remain missing. Armada's current TB322FC README explicitly reports
accelerometer unavailable and fixed display orientation; it does not provide an
existing working Gen4 sensor stream to copy. Use this HID component with real
mainline transport/sample evidence, not with a donor's default/fake producer.

## Evidence and reproduction

Mac accepted `lab/motion-hid-20261119/source5`, `native9`, `kernel2`:

-95 actual descriptor/units/event/demand checks and five actual native HID
 scenarios/152 checks pass ASan/UBSan. Native tests execute actual publication,
 bind, dispatch, refusal, demand rollback and stop methods with explicit OS
 peers. The host RTTI/virtual prefix is modeled; final original ABI is separately
 checked against the real OS17 image.
-Two native scalar objects compile without warnings and automatic initializers.
 The metaclass initializer is removed and called only during admitted start.
-Full kernel SHA256
 `c89d6000457e80613ceecc1fcee7f181d3c7e097ae66b463434585b9784f54eb`,
 native `e286e4faddaca7b4745451eca5d53475bfe0d3aaf02dc27ecc0c43e59fb8c5a5`.
 Actual constructor targets`0xfffffff004afb970`/`0xfffffff004afb9c8` and original
 dispatch`0xfffffff004aface8` are materialized and called through registers;
 the audit resolves those actual constants, not just a BLR count or string.
 IOHIDDevice207/10, parent metaclass, original constructor bytes and late
 initialization pass. Apple30, Actuator, pen and RTC/recovery audits pass again.
 Global prelink ABI/loadable/runtime/relocation flags remain false.

`prepare_motion_hid.py` qualifies an exact parent image and generates descriptors
and ABI; `build_motion_hid.py` runs actual host methods and compiles the native
archive; `link_motion_hid.py` links over the verified wake/Actuator/RTC/pen parent.
`audit_motion_hid_link.py` plus original component audits check that exact image.
Every output is a fresh folder; unsuccessful earlier fixture/header/build
attempts remain retained. There is no tablet access, boot installation or merge.

Workbench receipt: `lab/records/motion-hid-20261119`. Private archive:
`edl_work/motion-hid-20261119/motion-hid-offline.tar.gz`,19450151B, SHA256
`019ac72f97dab16e004f0c8554383a2c56f8b0b4a0fb132950a88be31ddbf7f9`,
all57 regular members verified. Mainline must review/port to its current
qualified source/transport and preserve Wi-Fi/PCIe/WCN/GPU/QRTR and recovery.
Next: actual Gen4 samples/attributes/clock/mount, native CoreMotion receipt and
complete J317 rotation/SPU contract; wake PM connection and camera pipeline continue.
