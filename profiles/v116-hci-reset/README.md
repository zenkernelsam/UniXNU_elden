# v116 Reset failure frame evidence

This diagnostic profile changes only WcnPowerNative.o over v115. Its source
retains the shared SPMI transaction gate and extends failure reporting to HCI
Reset/local-version stages. It does not retry commands, suppress errors or
change hardware sequencing. Build/audit using the v113 recipe with profile
v116-hci-reset, baseline v115 and the original v110 UART compile capture.

Hardware again completed958 segments, then stage19/-71 reported a complete
41-byte H4 ACL frame with reserved debug handle0x2edc. Only the first32 bytes
were logged; do not claim the complete payload was captured. The parser had
treated this controller log as the Reset completion. See hardware-result.json.
Capture1154517B/drop30; typed reset and exact stock restoration passed.
v117 filters only this complete, length-consistent reserved debug frame.
