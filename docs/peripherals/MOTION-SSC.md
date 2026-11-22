# Gen4 SSC client and native motion input

The real sensor client manager, strict attribute reader and native worker now
fully link over the accepted input-sleep kernel. The worker connects discovery,
actual firmware attributes, qualified units/mount/time and the existing OS17
accelerometer/gyroscope HID adapter. It has no automatic startup. The sole
mainline ADSP/QRTR owner still must install a real transport and board provider;
sensor-PD registry/FastRPC, real samples and UI rotation remain unaccepted.

The guarded `Orientation.o` consumer now ships in the SSC source-build archive
and current full-link candidate. Its four public entries are present in the
final image but have no initializer or outside call/data-reference edge. This
keeps the existing mainline worker, QRTR owner, HID producer and rotation UI
separate until a real J317 provider is qualified; it does not publish
orientation or install an automatic caller.

## Exact references

Armada's qualified TB322FC kernel is
`6f97250ead155fc3b980856323e99a4623dae73d`. Its actual board file is
`arch/arm64/boot/dts/qcom/sm8750-lenovo-elden.dts`, SHA256
`0a4361f3f2225befd954dd5420216f357bfa8a68294c28c895ec2478ebea3ba3`.
The SM8750 SoC file is191275B, SHA256
`cbf8bad5006ec6d2c3d654aeec94bb0f08be235e03804164a2252e5b65a4e175`.
They select ADSP/adsp_dtb and `fastrpcglink-apps-dsp`, with exact stream lists
`100N,104N,106N,108N` for compute sessions3..8; session4 has8 sessions.
Those DT labels do not identify a free hardware context bank or prove a running
sensor PD. Do not import the donor's fixed8MiB heap, address or bank4.

Protocol sources are the same-SM8750/same21H461 Pagani drivers at
`c888efd3ce1b817d13f28783a77cf689cb2255c9`, pinned in `ssc-source.json`.
Original `SensorsSsc`/`SensorsPb` bodies remain byte bound and retain their BSD
license; generated symbols/files are prefixed. Only the strict attribute datum
reader adds documented checks. This donor explicitly says host tested only.
Mainline's existing `ShengQrtr.c` owns IPCRTR and exposes no general client port
API; this branch does not start a competing router or mutate that owner.

## Implemented lifecycle

`Ssc.c` executes on one sensor worker. Connection0 discovers the default accel
and gyro SUIDs; connections1/2 independently query and configure each sensor,
matching original libssc's per-sensor clients. Every callback/request carries
owner, epoch and full service400/instance/node/port/generation. At most one
request per connection is outstanding; transaction wrap refuses. One report
may precede its initial QMI acknowledgement and is retained until the actual
client ID is known. A second unacknowledged report refuses rather than silently
dropping data. Wrong/stale transactions cannot satisfy an outstanding request.

QMI0x20 response is request acceptance, never a sample. Data use normal0x21
reports; jumbo0x22 is disabled explicitly in requests. New attributes require
complete type, API, available, rates, ranges, stream type and physical flag.
The parser rejects truncation, duplicate known fields/oneof/attribute IDs,
wrong wires, overflow, invalid bool/float/ranges and incomplete packets before
commit. Optional trailing string NUL and signed HW_ID remain legal. A reported
physical_sensor0 may describe an actual calibrated virtual stream; the real
board qualifier decides its source/unit/calibration, never a hardcoded default.

The board qualifier must approve real API/units/selected range and actual
mount; range must match the advertised firmware value. Missing attributes do
not become a donor default after timeout. Requested rate selects the lowest
advertised float rate >= actual demand and <=400Hz; no unsupported rate clamp.
The old T970 parser supplies hardened sample math only. Framework attribute128
and error130 IDs come from the correct newer donor; old T970 error6 is not used.

`NativeSscMethods.inc` is compiled and executed by the native-method host test.
It opens three subscriptions through the installed shared owner, copies bounded
callbacks, verifies all current server levels, and publishes HID only after
both real sensor descriptions and providers pass. It binds both event-driver
hooks before applying demands, avoiding the first-sample/start circular wait.
Workloop demand callbacks only update a latch and wake this worker; they do not
take over the router or issue hardware requests from the HID workloop.

Native start checks actual Darwin19.2MHz timebase. Each SSC sample additionally
requires an explicit real clock mapping, increasing source and mapped timestamps
and the existing HID age/future gates. There is no restamping, fake gravity or
identity-clock fallback. Batch sample envelopes are checked before publication.
Queue overflow, provider loss, server replacement and rollback close admission.
Callbacks are bounded; after assert_wait only the nonblocking event latch is read.

Stop closes public admission, asks real HID producers for zero rate, waits for
pending acknowledgements and requests disable. The5s manager/5.5s worker budgets
do not manufacture success. Provider withdrawal must prove queued-send/callback
fencing and remote inactivity or a really stopped old DSP generation. Queuing
DEL_CLIENT alone cannot satisfy it. On failure, handles, callback contexts,
native services and mappings stay retained until reset. Status separates original
runtime failure from terminal cleanup. Recovery stops motion before haptics,
RTC and UFS; the existing physical reset and floor gates are unchanged.

## Validation and reproduction

Accepted Mac outputs are `lab/motion-ssc-20261119/source6`, `native8`, `kernel6`.
26 actual manager/codec scenarios18372 checks and6 actual native-worker
scenarios614 checks pass ASan/UBSan. OS scheduling/QRTR/physical power/mount/
clock/HID peers are modeled. Five new scalar objects compile without warnings
or automatic initializers; the previously accepted MotionCore is now actually
linked once, with the original HID library retained.

Full kernel SHA256 is
`1640a2d3d5f0676db1d409b971324dd3e08a4a04994b7791e3032224bb680547`,
native `1041abbe2983b48fd3cd1367f09414f91a200773fda25cf3e222d2a5ca63ef63`.
Actual worker/parser/HID and motion→haptics→RTC→UFS calls pass final instruction
audit; Apple30, pen207/10, motion207/10, Actuator168/186/direct IOBuffer ABI,
RTC and camera/input-sleep checks pass. Global prelink ABI/loadable/runtime/
relocation flags remain false. Only one original UFS member is rebuilt for
recovery; original26 UFS warnings remain. Historical unsuccessful attempts
remain archived; no tablet or boot package is part of this delivery.

```sh
python3 tools/prepare_motion_ssc.py --output /new/source
python3 tools/test_motion_ssc.py --source /new/source \
  --motion-core /qualified/motion-core/source2 --llvm /path/to/llvm/bin \
  --base-native /qualified/v152/native --output /new/native
python3 tools/link_motion_ssc.py --parent /qualified/input-sleep/kernel4 \
  --ssc-build /new/native --motion-core-build /qualified/motion-core/host2 \
  --recovery-source /qualified/actuator-native/kernel4 \
  --prelink-tool /qualified/prelink-tool --original-kernelcache /original/21H461 \
  --output /new/kernel
python3 tools/audit_motion_ssc_link.py --kernel-build /new/kernel \
  --parent /qualified/input-sleep/kernel4 --ssc-build /new/native \
  --prelink-tool /qualified/prelink-tool --output /new/kernel/ssc-audit.json
```

The inherited source-built v152 objects/headers are required qualified inputs,
not a claim that the current mainline kernel equals that image. Source port and
fresh rebuild must preserve mainline's later Wi-Fi/SMMU/touch/recovery changes.
Evidence/asset receipts: workbench `lab/records/motion-ssc-20261119`, private
delivery in `edl_work/motion-ssc-20261119`. Source stays on this branch;
research contains only clones. `ssc-validation.json` binds the final delivery.

Next connect the real shared client provider and Gen4 sensor-PD registry/
calibration/file service, qualify actual units/mount/clock and observe samples.
Then establish the historical J317 consumer/SPU contract before orientation
publication. Accel/gyro HID is not sufficient proof of automatic UI rotation;
an orientation0x20/8a service without complete SPU interfaces can crash CoreMotion.
Real camera power/frames, wake panel/system PM, haptics calibration/system playback
and RTC persistence still remain. The full five-part Goal stays active.
