# v110 same-board Linux FIFO packing

v103 repeat still stopped at initial IBS wake (tx1/rx0). Stock Android's
existing enabled Bluetooth responds to normal IBS in about3–4ms, with the
same UART firmware0x66 and SE DMA. Those are runtime wake timings, not a
cold-boot deadline argument. Same-board Linux6f97250e configures4x8 FIFO.
v110 adopts those vectors while retaining clock/pin/power/IRQ/timeout settings.

TX packs up to four bytes; TX_TRANS_LEN excludes padding. RX interprets
RX_LAST and its valid-byte count, and keeps up to three remaining bytes when
the caller's buffer is short. Baud changes refuse outstanding software bytes;
stop discards them only after physical radio reset and records actual bytes.
Source snapshots here keep earlier candidates reproducible.

621 host scenarios/2,988,689 assertions cover full five-firmware startup,
CTS/clock/lease faults and every1..67-byte length with1..9-byte caller sizes,
under ASan/UBSan. The first4x8 model was invalid because FIFO words changed
after the status snapshot; final model latches four-byte words or a stale
tail. That failed attempt remains in edl_work/bt-fifo-v110-20261114.

Full20 BT objects,2 existing hooks and2 runtime core objects were rebuilt;
30 original Apple execution regions, entry, firmware copies/runtime and
signed AVB/FAT all passed. Image2808869e…/session23b2b26ae93e2e9b keeps
1738713600/footer0/parent3. It is based on the independent v103/v94 Bluetooth
branch, not the v1098GiB display branch; both candidates remain intact.

**Neither hardware run reached UART.** Both stopped on GPIO observer status9
and concurrent ADC failure. Logs681654/drop27 and1146464/drop13, typed sync
return and exact stock restoration passed. See hardware-result.json. No
third blind retry. Next address shared SPMI transaction serialization; the
FIFO change is statically qualified but its hardware effect is unknown.

Use the v103/v101 reproduction recipe with --profile v110-bt-fifo in
build_wcn_kernel.py, audit_wcn_kernel.py and audit_entry_relocation.py. Keep
the exact v99 runtime and v94 parent. Boot pack/audit uses --wcn-report and
--bt-runtime. The first build lacked the inherited recovery include; the
final kernel-build2 copies its exact bytes, and the failed build is retained.
This profile does not change proprietary firmware or publish binary inputs.
