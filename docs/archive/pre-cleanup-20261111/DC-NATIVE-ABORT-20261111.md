# Native DC access result

The v61/power39 build added an opt-in, bounded DC 450/500 mA trial. Host checks passed (1432 state-machine checks, 50036 SPMI checks), along with actual-object, original ABI and image verification. This does not qualify hardware writes.

On hardware, the existing DA65/9 V/bootstrap restoration passed. The 19-register pre-snapshot and native cross-check passed, using the unique EE0 APIDs 702/704/705. The first store of value 9 to the DC writer FIFO at offset 0x10 raised a synchronous external abort (ESR 0x96000010). The SPMI command had not been submitted; no lower-value readback or stop-restoration was observed.

The native mapping uses the same v7 stride and writer resource as the board Linux source. Ownership and observer reads do not prove CPU write access to this window. The exact access restriction is unresolved. Do not retry the candidate, switch to foreign EE channels, or change ACL/fuse/firmware permissions. The failed GLINK raw write path also remains retired.

The existing panic route returned to fastboot after approximately 180.1 seconds. Stock Android was restored with complete image readback and verified boot/root confirmation. No persistent root, partition or identity changes were made. Full charging remains incomplete.

Next: review the precise OEM access path offline. Preserve the successful HID ID and gesture work; address the independently identified IconServices/CoreImage failure before attributing application stalls to UFS. GPU remains last. Complete private evidence is in the paired workbench handover HANDOVER-DC-NATIVE-ABORT-20261111.md.
