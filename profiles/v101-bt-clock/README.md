# v101 Bluetooth parent clock qualification

The complete candidate is now built, signed and hardware-tested:
`bd4fb2211b8f48fed74ce7623f8da6da3c40c9a8c9764180f7b8444b3553baf2`,
session `9d007744db52fc06`. See [boot-audit.json](boot-audit.json) and
[hardware-result.json](hardware-result.json). Parent600/300MHz qualification
and all six rail votes/readbacks pass, reaching power stage6. The combined
UART/peripheral/HCI startup then returns−110; its current log does not identify
which substage timed out, and HCI ready remains0. Stop/close, session-bound
reset-after-sync, actual fastboot and full-SHA stock Android recovery pass.
The1154692-byte capture reports13 dropped bytes; Bluetooth is not yet usable.

This explicit successor to v99 uses the
[Gen4 Taycan vote-run decoder correction](../../docs/WCN-PARENT-CLOCK.md).
The plan changes only the owned WcnPower C/header inputs; it also carries the
previously qualified shared OL-CPR helper consolidation from b3a900a.
The remaining26 owned inputs, all external firmware/OS/runtime assets, v99
menu, two core hooks, entry-assembly contract and shutdown source remain fixed.
No v100 touch-delivery or v95 paired-store merge is included.

The published v99 image stops at parent admission before UART/HCI. This profile
records the checked build inputs separately from Bluetooth usability.
The decoder has12 power lifecycle scenarios/344 host assertions and a scalar
ARM64/iOS17 component build. The full20-object native build, two existing hooks,
two core units,30 original Apple execution ranges and unchanged startup assembly
now pass; the candidate keeps the same source-built FD/loader/Data and v99 runtime.
The remaining26 owned inputs are compared in [input-comparison.json](input-comparison.json).

Use the v99 reproduction recipe with `--profile v101-bt-clock` for
`build_wcn_kernel.py`, `audit_wcn_kernel.py` and `audit_entry_relocation.py`.
Keep the original v99 BlueTool/runtime input and v94 parent build. The boot
packer/auditor select the exact permitted profile from `--wcn-report`, and
verify its plan, entry proof, runtime hashes and original executable ranges.
Old v99 profile hashes are not rewritten; it reproduces at7f0704c.

The menu still selects real mode4 and retains battery publication, thermal
limits,180-second panic return and eight-second auto boot. Only exact Y700
identity, fresh battery admission, stock floor1738713600/footer0/parent3,
complete boot_a readback and supervised Android restoration authorize a run.

Next separate UART-open, peripheral-start and ordinary-HCI error reporting,
using their actual stored diagnostics before changing any clock/pin/firmware
sequence. Do not label the accumulated−110 as a proven HCI command timeout.
