# v113 correct GIC guard and GENI FIFO route

Same-board Linux6f97250e enables SE_IRQ_EN0xf for FIFO operation. This
candidate applies that route and refuses UART acquisition/live operations
if its GIC SPI461 (INTID493) is enabled. It only reads GICD at0x16000000;
no CPU interrupt handler is installed and no GIC register is written. Its
header value is checked against the real SM8750 DTS before compilation.
The prior v112 had an address typo and is withdrawn.

Only UartPlatform.o/UartBackend.o replace v111 members. The SPMI gate,
power/clock/pin sequence, firmware, runtime,6GiB and recovery remain frozen.
The Linux0xf setting is restored to its original value during UART stop.
Failure snapshots add IOS, IO macro, command/config and pin values without
reading FIFO data. The v112 UART-only host regression also covers this
byte-identical platform C source; it does not establish hardware routing.

```sh
python tools/build_uart_route_kernel.py --profile v113-uart-gic \
  --baseline "$V111" --uart-build "$V110" --prelink-repository "$PRELINK" \
  --original-kernelcache "$ORIGINAL" --llvm "$LLVM" --output "$OUT"
python tools/audit_spmi_gate_kernel.py --profile v113-uart-gic --build "$OUT" \
  --baseline "$V111" --components "$GATE" --core "$V91_CORE" \
  --llvm "$LLVM" --output "$OUT/kernel-audit.json"
```

Continue with the existing entry/AMFI/boot recipe and --wcn-report/--bt-runtime.
Imageada89829…/session25cc7af63a7ce36c completed peripheral stages1..13,
then ordinary HCI version rejected atstage16. UARTtx79997/rx228; received
`040e120100fc00190c21000000586d000200022140`. Product0x21, ROM0x200,
SoC0x40210200 match; byte8 is0x0c preceding the12-byte version struct.
The old check incorrectly required it to be zero. v114 fixes that check.

Log694578B/drop2, no recorded thermal errors, stop/close and typed sync return
plus full stock restoration passed. One successful peripheral run does not
prove repeatability: v114 later again failed initial IBS wake. Do not claim
the route alone permanently fixed that issue. No ordinary HCI download or pairing.
