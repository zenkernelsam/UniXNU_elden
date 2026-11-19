# Gen4 CCI and native sensor control — offline

Exact TB322FC CCI commands/timing, a real native mapping/IRQ-drain adapter and
front/rear sensor binding now compile and fully link. The original sensor
tables/power/control/stream bodies feed the actual native CCI path in composed
tests. No physical camera power provider, CSI/VFE/DMA pipeline, camera frame or
OS17 camera publication is connected. This is a control milestone, not working
cameras or five-part Goal completion.

## Exact source and controllers

Armada's TB322FC README sources fixed Linux
`6f97250ead155fc3b980856323e99a4623dae73d`. The original `i2c-qcom-cci.c` is20396B,
SHA256`b5ed3d8e0f8e078793616aaa5208d895ebc7af3c8efa19cf0c625ab100d8ce08`.
`cci-source.json` and `audit_camera_cci_board.py` verify raw Git commands, v2
timing, exact CCI DTS nodes and board sensor associations.

| Board control path | CCI physical | GIC INTID | Internal I2C master | Sensor slave |
| --- | --- | --- | --- | --- |
| Rear S5KJNS | `0x0ac7b000` / CCI0 |458 |0 |`0x56` |
| Front GC08A8 | `0x0ac7c000` / CCI1 |459 |0 |`0x31` |

The previous core parameter named `master` selects the physical controller.
Front camera does not use CCI0's internal master1. Both controllers match
`qcom,sm8750-cci` with `qcom,msm8996-cci` fallback and the original v2 profile:
37.5MHz CCI,400kHz I2C, queue0 writes/queue1 reads,64/16 queue words,11B maximum
write and12B maximum read. Fast-mode timing is38/56/40/40/22/35/62/0/6/3 in the
original thigh/tlow/setup/hold/buffer/stretch/readhold/spike order.

`CciSource.py` retains the original read/write bodies and constants, adapting
only readl/writel to error-propagating owned providers. Three original byte
packing shifts now explicitly cast to u32: original promoted signed-int shift
of a highest byte>=128 was reproduced under UBSan. Intended command words remain
byte-identical; this is not a protocol change. The original GPL/source notices
remain with the source-derived objects. Failed original build evidence is saved.

Reads preserve the original sequence of register-address WRITE on queue0, then
READ on queue1. No guessed LOCK/WRITE_DISABLE_P/retry opcode is introduced.
Sensor writes preserve each repeated Samsung`0x6f12` data-port address. All
transfer/register read results stage privately and update caller output only
after actual completion, buffer-level, idle and final provider admission checks.
The leading read-data byte is skipped exactly as in the original source.

## Ownership, native and sensor binding

The caller must own the whole physical controller: both internal masters and
queues are checked idle before reset or use. Existing queued work, unknown/
master1 pending source, wrong read level, NACK, read/write failure, clock rollback
or revoked admission refuses future transfers. No foreign queue/status is
flushed to make progress. Per-transaction wait is bounded by the original100ms
limit and10000 polls; IRQ completion is never fabricated.

`NativeCci.cpp` retains exact CCI/GIC maps and exclusive ArmCpu registrations.
It keeps the parent masked, checks actual RWP/enable and active/inflight drain,
and clears only its owned458/459 pending bit. An attach refusal never masks a
peer's route. An unexpected primary delivery closes admission and preserves the
first actual error. All register access requires the external real board power/
clock/pin/owner provider, including its actual37.5MHz clock result. The native
adapter itself does not guess rail votes, change GPIO or adopt another board's
camera clock defaults. A revoked provider cannot be bypassed during cleanup.

`SensorBinding.c` connects the existing source-derived S5KJNS/GC08A8 core to the
actual native CCI register methods. Same owner/epoch/controller readiness is
checked before any sensor power. Actual board resource callbacks and CSI/VFE
receiver-ready/drain callbacks remain mandatory. There is no default successful
receiver or dummy frame source. A reversible stream-off is separate from final
shutdown: sensor/receiver STOP must complete before CCI HALT/reset/IRQ drain and
timing/mask restoration. Failed drain retains sensor rails/clock/CCI ownership.

CCI STOP requires actual HALT0 acknowledgment before master0 reset, both queues/
read buffers idle, original timing/mask restoration and final GIC drain. A
preflight failure before owned writes is not promoted to safe release. Repeated
STOP after successful terminal state does not re-touch hardware. All partial
native maps/controller/IRQ objects remain retained to reset. No automatic boot
entry/worker/IOKit camera or changes to existing recovery are added by the link.

## Offline evidence

Accepted Mac `lab/camera-cci-20261119/source7`, `host-native10`, `kernel1`:

-135 actual CCI scenarios/133603 checks pass ASan/UBSan: every tested transfer
 size, high-byte packing, read-word levels, wrong sources, reentry, full
 read/write failure sweep, atomic output and actual HALT/reset/restore paths.
-11 actual native scenarios/160086 checks execute `NativeCciMethods.inc`,
 original CCI command decoding and original front/rear sensor power/control/
 stream bodies. Both the direct core and delivered SensorBinding paths run;
 receiver-drain failures retain resources. OS/MMIO/GIC/clock and sensor power/
 receiver are explicit peers, not hardware observations.
-Three new scalar CCI/native/binding objects compile without warnings/automatic
 initializers. The unchanged source-derived CameraCore object is included once.
 Initial missing-header/signed-shift/diagnostic attempts are retained separately.
-Full kernel SHA256
 `5c952842a2868c6c00083c6500c0d2bf4fdbc1c5afd77b38aaa4243c4939f6ba`,
 native `5098cf849f1844db2a8fffb66a17c4693d751c1dae73a96fe03a152d6752f217`.
 Actual sensor/native/CCI call and tail targets, unchanged initializer count
 and absence of unqualified camera startup pass audit. Added objects shift
 inherited native/data layout; audited ADRP/data relocations bind exact pooled
 string content or the same closest global label/offset. Inherited object
 digests remain fixed. All30 Apple execution regions, Actuator168/186/direct
 IOBuffer, pen207/10, motion207/10 and RTC/recovery checks pass again. Global
 prelink ABI/loadable/runtime/relocation flags remain false.

## Reproduction and remaining work

Run exact-board Git audit, `prepare_camera_cci.py`, then `test_camera_cci.py`
with the fixed camera-core source and source-built v152 XNU capture. It runs
actual composition tests and compiles the CCI/native/binding archive.
`link_camera_cci.py` includes that archive plus the original camera core once
over the current fixed motion/wake/Actuator/RTC/pen parent. Run camera, haptics,
pen, motion and RTC audits on the resulting exact image. Every output is new;
no tool flashes, operates the tablet or merges mainline.

Workbench receipt: `lab/records/camera-cci-20261119`. Private archive:
`edl_work/camera-cci-20261119/camera-cci-offline.tar.gz`,19514675B, SHA256
`ed194fb479e4ed299ec8d5c796d12a39e348ff3538c99ea608c57254deb2783a`,
all79 regular members verified. Code is on `offline-peripheral-fixes`.

Next actual Gen4 shared-power/CAMCC/GDSC/CCI/MCLK/pin provider, verified rail/
sensor/EEPROM identity, CSI/CSIPHY/CSID/VFE/SMMU/DMA/IRQ and real RAW10 buffers,
then OS17 camera delivery. Current source IDs`38ee/08a8` remain expected IDs,
not independently observed Y700 responses. Armada qualifies camera control DTS
only; another donor's guessed camera addresses/voltages or synthetic CMIO test
pattern do not prove this tablet's camera. Mainline owns hardware qualification
and must preserve its Wi-Fi/GPU/QRTR/charging/floor/recovery fixes.
