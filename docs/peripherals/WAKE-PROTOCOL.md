# NT36536 wake protocol — exact OEM, offline implementation

The double-tap decoder and original low-power SPI command are implemented and
compile for ARM64/iOS17. GPIO/PDC IRQ, native worker pause/resume and actual
display/system wake remain to implement; this is not a live wake feature.
Current evidence is workbench `lab/records/wake-protocol-20261119` and
`drivers/EldenWake/validation.json`. No tablet/boot/mainline changes occurred.

## Original facts and implemented behavior

The604536B stock `nvt_36xxx.ko` is hash-bound to a3c9fce9. Its caller0x4228
passes `packet[1]>>3` and the original packet to gesture report0x144c. ID30 with
function byte2==1 uses byte3 as the gesture ID. The real13-case switch and
`gesture_key_array` map ID15 to KEY_WAKEUP143, other12..24 gestures to KEY_POWER116.
`Gesture.c` accepts only double-tap15 in an enabled, owned gesture path; other
gestures never become a power-key event. It checks original envelope bytes1..65
and preserves outputs for disabled/unknown/bad packets. Pen bytes66+ are outside
that checksum. Gesture packets must not also reach normal finger/pen delivery.

The actual low-power entry is in panel notifier0x4d48. SPI calls0x4fc8 and0x5104
send `{0x50,0x11}` deep sleep or `{0x50,0x13}` gesture;0x5118 calls
`irq_set_irq_wake`. Generic PM callbacks0x5c28/0x5c48 only modify flags/completion;
`nvt_gesture_switch` only changes policy. Neither alone enters chip low power.
`elden_nvt_low_power_command` adds these actual bytes to the existing protocol,
binds the known0x130900 event page first and retains existing failure poisoning.
No reset, IRQ or PDC register is written. Return0 proves SPI delivery only.

`audit_nvt_wake_oem.py` verifies actual ELF relocations/call names/switch/key
tables and runs1024 original caller/normalization/switch/key fragments in Unicorn
with synthetic RAM. It also checks real command argument instructions. This is
an instruction oracle, not an Android/CPU/MMIO/device simulation. Its fixtures
are consumed by the actual C decoder tests.3439 decoder/oracle/policy/integrity
checks and38 real protocol-transfer/fault checks pass ASan/UBSan. Two native
scalar objects compile; the protocol public delta is exactly one added function.
There is no initializer, new runtime call, full kernel/boot or hardware result.

## Required integration

The normal point worker must pause and drain its DMA/IO before sending a command,
release real finger/pen HID contacts, retain SPI/SMMU/maps and own the real GPIO162
wake route. Exact SM8750 PDC mapping is162→109; a GPIO number is not a GIC vector.
System-sleep and display-off routes need separate actual lifecycle evidence.
Resume must disable/drain wake delivery, restore real transport/firmware/PID/HID
and then resume normal frames. Never replace hardware wake with a permanent
full-power polling loop or fabricate route readiness from the mapping alone.
No native GPIO/PDC producer or system power callback is connected in this build.

```sh
python3 tools/analysis/audit_nvt_wake_oem.py --module /original/nvt_36xxx.ko \
  --output /new/oracle
python3 tools/test_nvt_wake.py --oem-audit /new/oracle --base-native /base/native \
  --llvm /path/to/llvm/bin --output /new/host-native
```

Mainline ports the protocol function with its current worker/firmware and IRQ
ownership, then tests display-off and actual system suspend separately. The
feature switch does not bypass lockscreen authentication. Whole Goal remains active.
