# v102 UART wake and version evidence

This v101 successor only adds bounded failure-register snapshots while the UART
owner is still live, and records the first byte already discarded during stop.
It does not change UART packing, clocks, pins, power commands or protocol timers.
The18-scenario UART/firmware host fixture passes ASan/UBSan. Full build, entry,
original Apple execution ranges, runtime and AVB checks pass.

Image `a02cfcfcebe06626646e1dfb4b09255e296e4209a47727418afa1ea2a85c2e79`,
session `8cdb320b5cab2b40`; workbench `edl_work/bt-wake-v102-20261113`.
Floor1738713600/footer0/parent3 and the v101 runtime/menu remain unchanged.

The first run stopped at SPMI preflight−5 before reaching the requested diagnostic.
One repeat after verified stock restoration reached peripheral stages1..13:
patch, NVM, TME-L and activation completed. UART wrote79997 bytes and read228.
Ordinary HCI stage16 then rejected version identity (−19); no ordinary HCI
firmware segments or native pairing completed. Both runs captured sync/reset
and restored stock with full SHA. See [hardware-result.json](hardware-result.json).

Correction to the v101 summary: its full capture already contains
`ELDEN_BT_UART acquire=0 stage=3`, followed by protocol-stage2 timeout, tx1/rx0
and one byte discarded at stop. The earlier WCN-only filter missed those lines;
the substage was available without another probe. Do not rewrite the raw record.

Next inspect the actual21-byte version response and peripheral identity before
changing the matching rule. The startup variability is still unresolved; do not
attribute it to packing or a fixed delay based only on one failed run. Normal
RF_CLK1 stays off as specified by the ArmadaOS Gen4 source chain.

Reproduce with the v101 recipe, selecting `--profile v102-bt-wake` in the WCN
builder, auditor and entry auditor. Use the same v99 runtime input. The boot
tools accept this explicit hash-bound profile through `--wcn-report`.
