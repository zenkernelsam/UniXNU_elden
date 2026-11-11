# Real userspace and USB CDC, 2026-11-07

Device-tested candidate boot SHA256 430db4156dbb48a3eb208325fd245941f45dc0f091a82fd260e1f817f735eaf5, stock boot index 1738713600@0.

Standard chosen/root-matching selects the original OS volume. Catalog RamBlock 25c57e0e9b68bf452dd1986914525edc9d2320bc publishes the original 512MiB volatile Data seed; actual APFS volume UUID E5D06647-CCB4-491D-A897-3C21B55703CF is selected. The verified original storage ABI passes 190 slots/24 overrides/11 shared imports. No manifest/authentication stubs were added, and the original APFS executable remains unchanged.

Actual results: SystemOS stage6/status0/readonly1/authorized1; RAM Data stage4/status0/readonly0; /sbin/launchd exec accepted and genuine PID1 EL0 BSD syscall observed. Windows COM13 receives the XNU console and replies to status: 26 samples simultaneously cache=0 data=0 init_exec=1 user_trap=1 configured=1 dtr=1 dropped=0.

The run later panics when watchdogd exits with namespace2/subcode5; SpringBoard also crashes. This proves bounded userspace and logging, not stable boot or working UI. Persistent Data remains disabled. The next task is to diagnose actual watchdogd and service failures. Do not bypass the critical-process panic without finding its cause.

Full raw log, signed image, incremental build/packaging scripts and handover are preserved in the sibling Y700-Elden project under edl_work/ramblock-20261107 and research/HANDOVER-USERLAND-USB-20261107.md. No GPT, userdata or root-image writes were made in this iteration.
