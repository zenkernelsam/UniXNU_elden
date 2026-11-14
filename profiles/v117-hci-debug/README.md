# v117 real WCN7861 HCI initialization

Only Wcn7861Hci.o changes over v116. Complete H4 ACL frames whose handle is
exactly0x2edc and length matches are recognized as controller debug output,
following the pinned Pagani PbDebugHandle convention. Both quiet and command
completion loops may consume those frames within their existing deadlines.
Ordinary ACL, incomplete/malformed logs, wrong opcode/status and identity
failures are still rejected. No firmware command retry or fabricated response.

```text
py -3.12 -B tools/test_bt_startup_protocol.py --profile v117-hci-debug --dependency WORKBENCH/research/UniXNU-Pagani-Drivers --firmware WORKBENCH/lab/records/armada-bt-firmware-20261108 --output NEW_OUTPUT
```

The actual HCI fixture passes29 scenarios/7435992 assertions under ASan/UBSan;
the inherited cold-wake fixture passes1011229 checks. Build/audit with the
v113 recipe using `--profile v117-hci-debug`, baseline v116 and original v110
UART compile capture. Entry/AMFI/runtime/AVB stay on the inherited checked
recipe. `plan.json` binds source and parent hashes; build-result/kernel-audit/
entry-layout/boot-audit document the exact generated artifacts.

Hardware: peripheral firmware/NVM/TME-L, ordinary firmware/NVM958 segments,
Reset, post-patch version, Qualcomm identity and address checks all complete:
`hci=0 ready=1 patch=80ab segments=958`. The actual native H4 host is published.
But original21H461 bluetoothd faults12 times at file offset0x2f2fc0/FAR0x2a8,
before connecting to the host. Client0 and no traffic mean scanning/pairing
are **not** working yet. This is controller initialization, not end-user acceptance.

Image64f8c1e0033ee63d3fde3b5d266f4d353b545b7cb4716bf08ad1f91801c4d0db,
session5f04ffb636cb6c7e. Capture1164295B/drop25; typed sync/reset, physical
fastboot and full stock8b8e72da restoration passed. Final Android/root/green,
boot-completed and sleep requested. No tasks remain from this hardware trial.
Next adapt the exact17.7.11 native model factory/standard HCI route using
sheng's successful method, without importing its iOS27 offsets or capabilities.
