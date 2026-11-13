# v111 shared SPMI bus integration

This closes the component-only gate work in drivers/EldenSpmi. The existing
v110 core filelist is unchanged. Replace only Spmi.o in libEldenPmicClient,
BtPmic.o/WcnPowerNative.o in libEldenBluetooth, and link TransactionGate.o once.
All other archive members and inherited input hashes are verified. The audit
finds the actual acquire/release branches from ADC read/write, BT read and
BT_EN write, one gate state, unchanged start.s object and30 original Apple
execution ranges. The BT write helper was inlined into bt_set; the audit checks
that real function rather than requiring a nonexistent standalone helper.

```sh
python tools/link_spmi_gate_kernel.py --baseline "$V110" --components "$GATE" \
  --prelink-repository "$PRELINK" --original-kernelcache "$ORIGINAL" --output "$OUT"
python tools/audit_spmi_gate_kernel.py --build "$OUT" --baseline "$V110" \
  --components "$GATE" --core "$V91_CORE" --llvm "$LLVM" --output "$OUT/kernel-audit.json"
```

Use absolute paths and the v110 entry/AMFI and runtime packaging recipe.
The auditor also writes entry-layout.json; use a fresh output. Boot tools
select this profile through --wcn-report and the exact v99 --bt-runtime.
Do not invoke audit_entry_relocation.py separately for this profile.

Device imagee52e8005…/session249704f6af024889 crossed the prior SPMI failures:
54 ADC measurements succeeded, both port temperatures continued, thermal
protection armed, WCN survey and rails passed. UART4x8 acquired but initial
IBS wake still returned−110/tx1/rx0. This is one hardware window, not proof
that every historical status9 had the same cause or that charging is complete.
Log1,156,070B/drop11; typed sync return and full-SHA stock restoration passed.
Final build: Mac lab/spmi-v111-20261114/build. Binary/log assets stay in workbench.
