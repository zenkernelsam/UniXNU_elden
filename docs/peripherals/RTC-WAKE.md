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
GPIO/PDC/native worker/system-power delivery remains unimplemented.

Original NT36536 symbols include gesture report0x144c, gesture switch0x1a44 and
PM callbacks0x5c28/0x5c48. ID30/packet[2]==1 normalizes through packet[3], then a
12..24 switch. `nvt_gesture_switch` only changes a flag. Resume transport and
the actual native allowed-wake lifecycle still need integration/qualification.
Exact SM8750 PDC maps GPIO162 to109. Screen-off and full system-suspend are
different paths; permanent full-power polling is not hardware double-tap wake.

Mainline must review current clock/initializer/SPMI/recovery integration and
qualify actual PMIC epoch/offset retention, alarms and repeated suspend/wake.
No tablet access or automatic mainline merge has occurred here.
