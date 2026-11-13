# v114 real EDL version response length

v113 captured21 bytes of unified EDL Command Complete. Status is byte6=0,
subopcode byte7=0x19, byte8=12, followed by the12-byte product/patch/ROM/SoC
structure. The same-board Linux qca_read_soc_version copies from edl->data+1,
skipping this byte. Previous host fixtures incorrectly put zero there and
the port incorrectly required zero, rejecting the valid real reply.

This changes only that condition to length12. Command/event length, opcode,
status, product0x21/0x1e, ROM0x200, SoC0x40210200, cross-check to peripheral
identity and post-download patch version remain checked. The fixture now uses
the exact captured initial product/ROM/SoC and patch0x6d58; after simulated
download it expects the real asset's patch0x80ab. Tests reject0/11/13 lengths
and nonzero status. Actual old source reproduces the valid-reply rejection;
fixed25 scenarios/6,169,667 assertions pass ASan/UBSan.

```text
py -3.12 -B tools/test_hci_version_length.py --dependency WORKBENCH/research/UniXNU-Pagani-Drivers --firmware WORKBENCH/lab/records/armada-bt-firmware-20261108 --output NEW_OUTPUT
```

Build with the v113 recipe using --profile v114-hci-length and --baseline
V113; --uart-build remains the exactV110 compile capture. Only Wcn7861Hci.o
changes. Use audit_spmi_gate_kernel.py with the same profile, inherited core
and gate paths, then the normal entry/AMFI/runtime signing/audit recipe.

Signed image62e98a5d…/sessiond1fd564f61ab78da did NOT reach the new check:
initial IBS again timed out tx1/rx0. Thus the version fix is real-packet/host
verified, not yet live-download verified. Shared SPMI/thermal and recovery
remain healthy; see hardware-result.json. Next investigate cold startup/port
reopen synchronization using the same-board Linux and original HAL, without
loosening identity checks or repeating the same boot solely to get lucky.
