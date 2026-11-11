# Fixed DC same-value qualification

The input current remains 500mA. This opt-in hardware test writes raw10 once to bus0/SID7/0x2a52, only after the actual 9V PD sequence has restored the existing 500mA budget. It does not expose arbitrary addresses, values, FET controls, higher current or role changes.

The loaded stock pmic-glink-debug module establishes the 28-byte WRITE request and 16-byte ACK. The ACK does not echo its address, so a full 19-register before/after read is mandatory. Both GPIO descriptor blocks and path/suspend/USB/DC states must exactly match the previously observed stock 9V condition.

One-second completed thermal policy credentials, three-second coherent sole i_sdp=500 vote, current BMS/UCSI/epoch and no pending control are required immediately before the write. Unknown completion is never retried. Stop prevents a new write and drains an existing one plus readback before the old shutdown path, with an explicit bounded unconfirmed result.

Power32/basic56/backlight13 uses frozen sources. 1994 new and932 read-protocol ASan/UBSan checks pass; native objects/archive, no FP/SIMD and shutdown call order were independently checked. Additional UCSI pre-ACK event handoff guard has424+1845+796 host checks. Candidate root derivative SHA256:30c981fc3d111e852ed486afb30b8a4bea57a1ca8414409f9e53b7fde5006d16. Hardware result is pending; host tests do not establish higher-current safety or full dual-port protection.

Detailed workbench evidence: research/input-budget-20261110/noop-source-review.json, noop-integration-review.json; edl_work/basic-bundle-20261108/v56/dc-noop-codegen/audit.json; live-dc-noop-v56-20261110.

## Hardware outcome

v56:1173697B/drop0,57 UCSI-ready cycles/ACK5/pending0. PD never reached9V;the DC operation stayed unstarted until orderly stop (attempted0/ack0/verified0). New stop/sync/fastboot passed. The exact prior-successful v55 control also stayed5V today:1383842B/drop0,9 CRC-valid firmware log pages,clean return. This demonstrates the symptom is not unique to v56,not that every new-code effect is excluded. Both runs restored stock Android/full SHA/root/green. DC same-value WRITE remains hardware-unverified.

Full workbench handover:research/HANDOVER-DC-NOOP-20261110.md. Compare the type-dependent stock USB_SET property5 callback and firmware-owned handoff timing;do not treat100mA as a universal fixed-current restore. No higher-current experiment was performed.
