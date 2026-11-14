# v125 bootstrap and H4 phases

v124 crossed the owned BlueTool launch check but USB stopped responding before
any H4 connection was captured. This diagnostic separates kernel bootstrap
status handling from native connect/send without changing their return values.

`drivers/EldenBluetooth/diagnostics/TransportTrace.py` checks the exact two
current source hashes, inserts bounded logs, and reverses every insertion to
verify the original bytes. The first8 bootstrap reads and connections and
first16 send callbacks are observed. No addresses, key material or packet data
are printed. Logs are outside the transport lock; no new timer is added.

`BootstrapStatus` reports entry, stable snapshot/state and SYSCTL_OUT return.
`NativeHost` reports connect entry, accepted process identity, acquired process
reference, before/after bridge connect, and before/after send processing.
Existing refusals may return before the final marker; a missing marker after
USB discontinuity does not prove the callback did not execute.

The two source units are rebuilt through the clean build's closed overlays.
Their original exports, scalar code and initializer policy are checked. Only
their members in libEldenBluetooth change; the core filelist and other archives
are retained, with all30 original Apple executable regions checked after prelink.
The initial compile rejected comparing XNU's integer user_addr_t to nullptr in
the new print statement; it was corrected to an integer comparison. No original
driver condition changed. The successful second build is the packaged input.

Build from the declared original inputs using:

```sh
python3 -B tools/build_clean.py --variant bluetooth --bt-exit-stack \
  --software-keystore --bluetool-launch --bt-transport-trace \
  --inputs inputs.json --output NEW --target boot
```

The complete source baseline and appended stages have each been executed.
The combined flags have not been rerun as another full empty-directory build.
Direct delta entry: `tools/build_bt_transport_trace.py --clean-build CLEAN_OUTPUT
--bluetool-build OWNER_OUTPUT --original-kernelcache ORIGINAL --llvm LLVM_DIR
--output NEW`. Both parents are products of this repository's source pipeline.

Boot SHA `d58f5133a3933480a8815ff5122a69e2b2e90b6c7081c1583b1d9b29ce813880`,
session `fbaa89b75e0ff451`. The mode2 menu, firmware, Data, runtime/trust and
floor1738713600/footer0/parent3 remain unchanged. Hardware evidence is separate.

Hardware: two bootstrap reads both reach snapshot state2 (Ready) and return0.
No native connect/send marker or H4 connection is captured before USB Write
timeout. Capture109806B, reported drop7, then bounded reconnect expires. This is
not proof that no callback executed after the discontinuity. Physical fastboot
returns automatically, exact reset cause unproven; full stock SHA and Android
root/green/boot completion pass, sleep requested. See hardware-result.json.

Next inspect exact-original bluetoothd's socket/CTLIOCGINFO/connect syscall
entry/return. Its original UART.SK open starts at0x1000ddb7c, socket call
0x1000ddbb8, ioctl0x1000ddc10 and connect0x1000ddd04. These addresses were traced
in the exact21H461 original, not borrowed from27. The native F3 rule at0x1000ddd20
remains unchanged. Only the two observed bootstrap calls can be ruled out as
remaining inside the handler; later process/USB state remains unobserved.
