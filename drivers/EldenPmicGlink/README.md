# Elden PMIC GLINK board adapter

`ChargeRead` implements only scalar property GET messages. It emits no charger,
power-path, firmware update, GPIO or shipping command. `PmicClient` now uses the
original sheng ADSP/GLINK transport, with one channel owner, fresh link epochs,
complete45-field polls, timeout quarantine and two-port UCSI GET/command ACK.
The full chain is compiled and signed; device charging remains unqualified.

The explicit `elden-ucsi-init=1` profile now completes the stock v1.1 PPM reset
and basic notifications before querying both ports. It was device-tested with
one USB data partner and one PD partner; this does not establish net charging.
`ChargeNotify` adds a separate opt-in `elden-pmic-notify=1`: Lenovo's exact
zero-payload notification registration followed by its 200 ms settling time.
Malformed/late replies cannot trigger retries or a current-setting command.

The stock TB322FC module confirms owner 32778, Lenovo GET/SET opcodes 0x60/0x61,
24-byte requests and global input-suspend property 27. The remaining diagnostic
IDs are pinned vendor-source candidates and require live readback. `input_suspend`
is boolean in the stock interface, not a per-port mask.

Keep firmware-owned thermal, overvoltage and reverse-current protection. Preserve
the `ChargeRead` GET-only boundary while qualifying the original charging path.

`BatterySnapshot` converts only an explicitly qualified Lenovo aggregate profile:
0.01% SOC, uV/uA/uAh and0.01C temperature. Current direction must be established
against OEM Android; FG1/FG2 vendor scalars remain independently labelled raw.
`BatteryRuntime` publishes through the native source-kernel IOPMPowerSource, with
deferred class registration. Defaults are off. It never invents zero SOC, sums
USB/WLS inputs, averages unequal cells or publishes guessed AdapterDetails.
Loss of telemetry withdraws charge/current confirmation, marks unavailable and
retains the last measured capacity. It does not change hardware charging.

See `docs/BATTERY-TELEMETRY-20261108.md` and the work-repository research handover.

The optional `elden-charge-control=1` controller uses the original Lenovo
GET 0x60 / SET 0x61 interface. `ChargePolicy` captures the original protection
window, offers stock restore, charge bypass, 76–80% and 40–60% windows, and
verifies writes through window readback and subsequent complete BMS cycles.
Only properties 28/29 are written. Temperature, float-voltage, calibration,
fake-SOC and identity commands are outside this interface.

The board USB console accepts `charge SESSION stock|bypass|80|40-60` and reports
an applied generation separately from command receipt. All four controls and
restore have passed a native sequence. USB's permitted system-input current
is independent of battery charging permission: bypass/window holds retain the
configured 500mA budget. Real 80% and 40–60% full cycles and daily-use settings
persistence remain to be qualified; the existing Data volume is volatile.
