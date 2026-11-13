# v103 SPMI completion and HCI version capture

The pinned observer reader now follows the ArmadaOS Gen4 Linux v7 contract:
wait for DONE, then classify the completed error bits. One command and the
existing bounded poll remain; completed failures still stop. The actual old
reader fails a pending-error case and the derived reader passes17 ASan/UBSan
scenarios. `SpmiReadPatch.py` fixes only this function in the exact pinned blob.

The complete kernel, entry and signed image are checked. Image
`9f466b0c25f2041f3d8225de36c6d6904ee06eb221fd77aef9ac7449b915ddde`,
session `ad72e41099cb2ed2`, keeps floor1738713600/footer0/parent3 and v99 runtime.
The profile also records the last GPIO observer result and the public HCI
version frame on an identity failure; it does not weaken the identity checks.

This run passed parent/rails/UART acquisition but stopped at initial peripheral
IBS wake, stage2, tx1/rx0. Its failure snapshot had no queued RX word. Stop later
discarded a zero byte after controller reset; this does not prove a delayed ACK.
The version capture was not reached. The1154962-byte log reports24 dropped
bytes. Sync return and full-SHA stock Android recovery passed; see
[hardware-result.json](hardware-result.json).

v102 had previously completed peripheral patch/NVM/TME-L/activation once and
stopped at ordinary HCI version identity. Startup repeatability and the actual
version tuple remain unresolved. The SPMI correction does not retrospectively
prove the cause of the earlier−5. UART clocks/pins/protocol deadlines are unchanged.

Use the v102 reproduction recipe with `--profile v103-bt-version` for WCN build,
kernel audit and entry audit, then the boot tools with this `--wcn-report` and
the unchanged v99 runtime. Native Bluetooth, pairing and sleep remain unverified.
