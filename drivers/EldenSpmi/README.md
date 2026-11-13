# Shared bus0/EE0 transaction gate

v110's first and single repeat both stopped before UART at a completed SPMI
status0x9, while the ADC/thermal worker also failed. Same-board Linux calls
this DONE+transaction-dropped (bit3), not permission-denied (bit2). Its
spmi-pmic-arb.c serializes whole transactions with the bus lock. Existing Elden
ADC and WCN owners protect their resources independently but did not serialize
the shared bus. This is a concrete missing synchronization contract; it is
still a hypothesis for the observed hardware failures until tested.

TransactionGate owns one atomic bus word. ADC reads/writes and WCN GPIO/rail
reads plus GPIO writes acquire it around command/data/completion. Operations
are worker-only and use bounded polling without sleeping/logging while held.
Waiting callers are refused after a bound without issuing commands. A timed
out hardware transaction poisons the gate until reboot; completed error
statuses release it but remain errors. There is no automatic transaction
retry, access-policy modification or successful-error substitution.

SourcePatch derives three exact frozen sources. It also gives the BT writer
the already established DONE-before-error ordering. It preserves address
whitelists, per-device leases, voltage policy and output verification.
The independent SDAM recovery remains outside the normal gate after stopping
workers; it must not depend on a potentially poisoned normal-bus lock.

Host concurrency uses the actual ADC/BT reader bodies and an injected
single-transaction hardware model. The original bodies collide; gated bodies
serialize. This validates the code's behavior, not the hardware model itself.
Contention refusals are counted separately from successful transactions; they
are not silently retried in the actual-reader test. Completed-error release
and timeout poisoning were checked, including unchanged error output bytes.

```text
py -3.12 -B tools/test_spmi_transaction_gate.py --dependency WORKBENCH/research/UniXNU-Pagani-Drivers --output NEW_WORKBENCH_LAB_DIRECTORY
python tools/build_spmi_gate_components.py --baseline V110_BUILD --llvm LLVM_BIN --output NEW_MAC_LAB_DIRECTORY
```

Final native components are in Mac lab/spmi-gate-20261114/components3. Four
objects (one provider, three real consumers) compile ARM64/iOS17 without SIMD
or automatic initializers and preserve prior exports. The first two build
attempts could not resolve the captured relative meta_features header; final
build uses its verified compile-directory mapping. Attempts remain retained.

**Not yet linked into a complete kernel or tested on hardware.** Next replace
Spmi.o in the PMIC archive, BtPmic.o/WcnPowerNative.o in the BT archive, and add
TransactionGate.o once; prove all three consumers bind that one provider and
the source/entry/AVB/recovery contract before the next device run. Do not claim
the FIFO candidate or the shared lock has fixed Bluetooth or charging yet.
