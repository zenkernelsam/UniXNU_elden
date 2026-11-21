# RTC and wake — offline implementation

The real RTC provider and board wall-clock callbacks compile and fully link.
They do not yet provide persistent epoch, alarm or system sleep/wake acceptance.
Current evidence is `drivers/EldenRtc/validation.json` and workbench
`lab/records/offline-peripherals-20261119/rtc-20261119`.

## Implemented RTC path

`Source.py` extracts fixed TB322FC Linux6f97250e `pm8xxx_rtc_read_raw`, including
its low-byte carry reread. `CorePublic.inc` validates enable/state, preserves
failed outputs, learns a volatile offset when the system sets time, and latches
counter rollback. The provider reads PMK8550 SID0/control0x6146/counter0x6148..4b.
It never writes the raw counter, alarm or an assumed SDAM cell.

## Board facts and alarm bank

Captured DTS evidence (`lab/records/cpu-activation-20261111/.../pmk8550.dtsi`,
ArmadaOS TB322FC kernel bf96c3d per `linux-source.json`): node
`pmk8550_rtc: rtc@6100` compatible `qcom,pmk8350-rtc` with
`reg = <0x6100>, <0x6200>` ("rtc"/"alarm" banks) and
`interrupts = <0x0 0x62 0x1 IRQ_TYPE_EDGE_RISING>`. The board dts declares no
`nvmem-cells` "offset" cell, no `qcom,uefi-rtc-info`, no `allow-set-time` and
no `qcom,no-alarm`; absence in this DTS is a declaration gap only and does
not prove no accessible cell exists on the physical PMK8550.

The exact TB322FC source is retained at
`research/tb322fc-linux-kernel/arch/arm64/boot/dts/qcom/pmk8550.dtsi`; its
SDAM2 node declares reboot-reason storage but no RTC offset cell. ArmadaOS-
Freedom's Gen4 device record
`research/armadaos-freedom/devices/lenovo-tb322fc/camera/DEVICE-TEST-20261118.md`
reports a 45-second RTC alarm wake and subsequent battery-only 600/1200-second
s2idle RTC wakes. These Linux results provide a board-level functional
acceptance reference, not an XNU NVMEM cell or XNU IRQ/provider implementation.

Pinned register table (donor `pmk8350_regs` in
`drivers/EldenRtc/upstream/rtc-pm8xxx.c`, tb322fc-linux-kernel@6f97250e):
counter ctrl/write/read 0x6146/0x6140/0x6148 and **alarm** rw/ctrl/ctrl2
0x6240/0x6246/0x6248 with enable bit7 and status-clear bit0. An earlier draft
addressed alarm traffic at 0x61xx, which is the counter bank; programming
"disable" there would have cleared the RTC enable bit. All alarm I/O now uses
0x6240/0x6246/0x6248 exclusively.

## Tested core interface and provider model

Host-tested (`drivers/EldenRtc/TestCore.c`, assert-based PMIC/NVM/alarm
models; not a physical test): `elden_rtc_core_raw` donor Gen4 carry/reread
plus rollback->CounterReset; `elden_rtc_core_learn` volatile offset;
`elden_rtc_core_bind_persistence`/`restore_offset`/`learn_persistent` and
`elden_rtc_core_bind_alarm`/`alarm_set`/`alarm_read`/`alarm_ack` explicit
providers; `elden_rtc_core_persist_state` None/Uncertain/Verified;
`elden_rtc_core_stop` gating. The core never picks a cell or SDAM address:
unbound paths return `EldenRtcNoProvider` and every provider call refuses
with `EldenRtcStopped` after stop, before any provider I/O.

Restore reads the live counter before touching NVMEM and validates the
stored value; rejected or failed NVM access leaves the prior accepted RAM
offset intact and marks the state Uncertain. Persistent learn writes the
cell, reads the four bytes back, and only publishes RAM+Verified on exact
equality; a failed write/readback or mismatched data keeps the prior
accepted RAM and marks Uncertain. A counter rollback invalidates the
offset's Verified claim.

Alarm set follows donor order RMW-disable -> 4-byte data write -> RMW-enable,
preserving unrelated control/status bits; enabled requests at or before now
are rejected before any register access, and the live counter is re-read
after programming but before enable so an overrun deadline stays disabled
with the confirmed data value exposed. Ack follows donor handler order
RMW-disable -> RMW-clear status bit0 and is for the serialized clock owner
to invoke from a real IRQ path only. A failed register write may have
partially applied and raises `alarm_uncertain` until a fully completed
set/read/ack resynchronizes the cache.

Neither provider is bound in the current build: the SPMI transport stays
counter-read-only (no NVMEM writer, no alarm-bank owner) and there is no real
alarm IRQ owner, so PMK8550 0x62/1 edge-rising delivery and system wake are
explicitly unvalidated.

`drivers/EldenRtc/Owner.{c,h}` is the guarded owner-facade source contract a
mainline clock owner can later bind to the real named NVMEM offset cell and the
real PMK8550 0x62/1 alarm owner. It is default-off and chooses nothing: each
provider must declare a name equal to the caller's, a nonzero owner/epoch and
all serialized callbacks before `elden_rtc_core_bind_persistence` or
`elden_rtc_core_bind_alarm` runs; it never selects a cell, SDAM offset, bank or
IRQ from a filename or donor default. Guarded restore/learn/alarm_set/
alarm_read/alarm_ack/stop/status re-check owner/epoch and reject stale
generations before provider I/O; a failed provider operation keeps the existing
core uncertainty and retains the bound handles; stop is facade-level (it gates
the facade's guarded operations, not the live core's clock lifecycle), and a
stop landing inside any provider callback — `owner_epoch` included — blocks the
pending read/write, blocks later publication and retains the registration until
an explicit stop->retire fence that also refuses while a callback is active and
leaves the facade reusable for a new qualified bind (prior generations stay
stale); alarm acknowledge is callable only from the owner-supplied IRQ-path
identity bound at `bind_alarm`. Donor register ordering and the
recovery 5500ms gates are untouched. HOST-ONLY synthetic provider peers prove
the fences; `Native.cpp` binds nothing and no physical NVMEM/alarm/IRQ exists.

The owner facade now uses two nonblocking atomic claims. The operation claim
covers each complete restore/learn/alarm/status/bind/retire lifecycle through
the core call and publication, while the nested busy claim serializes provider
callbacks. Stop during a callback or an in-flight second bind aborts later
provider I/O and publication, and stale generations remain fenced. WSL and
CiscoMac pass 112 owner checks plus 308 deterministic pthread checks under
ASan/UBSan and TSan; scalar ARM64 `Owner.o` objects are warning-free. These
are host/model checks only and do not bind a physical provider.

The actual v7.2 SPMI PPID061 selection is unique and observer-only. Native maps
core0xc400000, owner0xc42d000 and selected observer0xc440000+APID*0x20. The only
MMIO write issues a read command in that observer. Shared TransactionGate owner3
serializes it with ADC1/BT2; a timeout poisons the real shared gate. IRQ context,
concurrent calls, terminal stop and retained mappings are explicit.

The existing ArmDTPlatform source at07f96a42 is preserved except upstream
e8bd8d5's clock registration/date/get/set block and its two overrides. If no
qualified board time exists, it uses the upstream kernel-build-date fallback.
That is calendar initialization, **not the AVB rollback/floor index**, which is
unchanged. Sheng's board-specific SDAM0xbc is not a Y700 offset source.

The one automatic RTC initializer only registers actual clock callbacks. No
mapping/read occurs unless `elden-rtc=1` is explicitly selected. Link1 replaces
only ArmDTPlatform.o, one shared TransactionGate.o and actual ShengUfs recovery
object, then adds libEldenRtc.a. The actual RTC-stop BL precedes UFS-stop BL in
the same recovery function. Stop uncertainty retains mappings until reset.

## Offline validation and reproduction

Real carry/output/rollback tests, SPMI whitelist/contention/DONE-error/poison
tests and the extracted upstream platform get/set methods pass ASan/UBSan.
Native2 has five scalar warning-free objects. Kernel1 preserves30 Apple
execution regions; pen ABI was rerun and passed. `audit_elden_rtc_link.py` verifies
one register-only initializer, exact callback addresses and recovery call order.
It also emits actual object section byte extents; the old build report's value3
was the length of `[extent,flags,digest]`, not a three-byte code section.

```sh
python3 tools/prepare_elden_rtc.py --output /new/rtc-source
python3 tools/test_elden_rtc.py --source /new/rtc-source --output /new/rtc-host
python3 tools/test_rtc_owner.py --llvm /path/to/llvm/bin --output /new/rtc-owner
python3 tools/build_elden_rtc.py --source /new/rtc-source --base-native /base/native \
  --llvm /path/to/llvm/bin --output /new/rtc-native
python3 tools/link_elden_rtc.py --parent /base/pen-kernel --base-native /base/native \
  --rtc-build /new/rtc-native --recovery-parent /base/ufs-wait-trial \
  --prelink-tool /base/ufs-wait-trial/prelink-tool \
  --original-kernelcache /path/to/original-kernelcache --output /new/rtc-kernel
python3 tools/audit_elden_rtc_link.py --kernel-build /new/rtc-kernel \
  --parent /base/pen-kernel --rtc-build /new/rtc-native \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --output /new/rtc-kernel/rtc-link-audit.json
```

## Wake implementation remaining

The actual OEM command/decoder foundation now compiles, documented in
[WAKE-PROTOCOL.md](WAKE-PROTOCOL.md). ID15 maps to KEY_WAKEUP143; panel paths
send50:13 gesture or50:11 deep sleep.1024 original instruction cases and3439
decoder/38 SPI checks pass. Generic PM callbacks only update flags/completion.
GPIO/PDC/native library and subsequent original-worker reversible pause/resume
now have code/link evidence in WAKE-IRQ.md and INPUT-SLEEP.md. Actual board
system/display-power provider and physical wake remain incomplete.

Original NT36536 symbols include gesture report0x144c, gesture switch0x1a44 and
PM callbacks0x5c28/0x5c48. ID30/packet[2]==1 normalizes through packet[3], then a
12..24 switch. `nvt_gesture_switch` only changes a flag. Resume transport and
the actual native allowed-wake lifecycle still need integration/qualification.
Exact SM8750 PDC maps GPIO162 to109. Screen-off and full system-suspend are
different paths; permanent full-power polling is not hardware double-tap wake.

Mainline must review current clock/initializer/SPMI/recovery integration and
qualify actual PMIC epoch/offset retention, alarms and repeated suspend/wake.
No tablet access or automatic mainline merge has occurred here.
