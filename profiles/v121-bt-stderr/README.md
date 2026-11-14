# v121 bounded native stderr observation

Based on v119 (not v120). Only sys_generic.o changes; its writev_internal path
copies at most512 bytes/eight iovs for fd2, at most16 writes, and only exact
bluetoothd name/original CDHash. It does not advance or modify uio, change syscall
results or alter launch/security policy. Host tests cover identity, bounds,
copy failures and unchanged uio. Native scalar build, original30 Apple components,
shared SPMI consumers, entry/FAT/AVB and all retained inputs pass. First build's
new vector instructions were rejected; final stderr121b uses explicit scalar
codegen. No generated source or object is taken from that rejected build.

Hardware stderr shows real device-address discovery (marconi-bt absent, bluetooth
local-mac-address found), then preference cleanup, followed by exit1. It does not
yet identify why startup ends. Later launches have the existing category refusal;
do not bypass it as a presumed root cause. HCI initialization remains successful,
native host has no client. Full raw evidence is summarized in hardware-result.json.
Typed sync, real fastboot and full stock restoration succeeded; Android/root/green
and sleep request confirmed. No device task remains.

The user relayed Jerry's inability to reproduce from a clean clone: incremental
builders depend on historical build directories. Finish source-build closure and
verify in an isolated root before resuming further Bluetooth probes.
