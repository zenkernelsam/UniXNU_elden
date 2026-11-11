# Firmware policy trace

The v57/power33 diagnostic profile adds a fixed ULog override category0x402000020/level3 (USB charger5,DPM25,ICM34),twelve current pages at least5s apart,and cold defaults override0/level4 restoration. Exact stock firmware proves the32B SET19/16B ACK ABI. Existing charge-control execution and policy cores are byte-identical to power32.

163 new plus326/24643 existing sanitizer checks and native no-SIMD/archive/ABI checks passed. Device capture1459326B/drop0 yielded12 CRC pages (5 empty);configuration and restoration each receivedACK. Orderly shutdown,sync,fastboot and verified stock Android/root/green restoration passed.

Actual input1 PD5V3A/source3/type15 briefly changed back to source2/type2/SCP_SVS500. doneSts0,health-limit0 andEnableChargeLimit0 at these events do not support the simple end-of-charge explanation. Stable9V and dual-port fast charging remain incomplete. Next inspect the firmware source+0x20 selector and logging at6005e730. Do not infer electrical isolation from source IDs or force input switches.

Frozen inputs and detailed handover are in the paired Y700 workbench research/basic-bundle-20261108/source-compiled-v57 and research/HANDOVER-PD-POLICY-TRACE-20261110.md.
