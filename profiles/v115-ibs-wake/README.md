# v115 bounded cold IBS wake

The exact TB322FC stock HAL retries the idempotent FD wake byte ten times,
with a 10ms receive wait per attempt. `provenance.json` binds its input and
function bytes. This profile follows that bounded timing, requires a real FC,
and never repeats a firmware command. Partial H4 reception prevents another
FD. Errors and timeout remain errors; stock's exhausted-loop success is not copied.

The startup fixture covers delayed replies at attempts2–10, no reply, partial
H4 and a stuck clock. The full HCI fixture retains all firmware, status and
identity checks. Reproduce both with:

```text
py -3.12 -B tools/test_bt_startup_protocol.py --profile v115-ibs-wake --dependency WORKBENCH/research/UniXNU-Pagani-Drivers --firmware WORKBENCH/lab/records/armada-bt-firmware-20261108 --output NEW_OUTPUT
```

Build/audit with the v113 recipe, `--profile v115-ibs-wake`, baseline v114,
and the original v110 `--uart-build`. Only Wcn7861Startup.o changes. Continue
the inherited entry/AMFI/boot runtime recipe; see build-result/boot-audit.
No original firmware or Apple executable is distributed here.

Hardware completed peripheral initialization, ordinary version and958 firmware
segments, then rejected the Reset response at stage19/-71. Capture1156422B,
drop3; typed reset and full stock restore passed. No HCI-ready or pairing claim.
v116 diagnoses the unexpected frame; v117 subsequently reaches HCI-ready.
