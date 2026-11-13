# v107 IOKit wait diagnosis

The v106 About thread waited in SystemConfiguration's IOKit initialization.
This profile adds a bounded observer adapted from sheng's busyReport to the
existing display glue, without changing busy counters or matching behavior.
Six complete snapshots at 10/30/60/120/240/300 seconds show root busy=0,
AES present, IONetworkStack absent (providers=2). See hardware-result.json.

Reproduce from the exact v106 build in plan.json and the v104 glue compile
capture with tools/build_iokit_busy_kernel.py. Inputs: --baseline, --glue-build,
--prelink-repository, --original-kernelcache, --llvm, --output. Use absolute
paths. Then run tools/audit_iokit_busy_kernel.py with --build, --baseline,
--llvm and a new --output. All other core/archive members remain frozen.
Use audit_entry_relocation.py --profile v107-iokit-busy, prepare_v91_entry.py,
then build_v91_boot.py/audit_v91_boot.py with --hid-report. The inherited
asset and signing recipe is the v106 recipe; its floor and recovery are kept.

The first source attempt referenced a nonexistent IORegistryIterator header;
the final source uses IORegistryEntry.h. The first audit matched a data label
alongside the worker; final audit requires the exact worker symbol. Failed
outputs remain in the workbench/Mac lab. No silent qualification relaxation.

Raw capture: workbench edl_work/live-iokit-v107-20261114; 1,513,036 bytes,
drop82. Typed RESET_AFTER_SYNC and complete stock restoration passed.
This diagnoses a missing provider, not a proved permanent IOKit busy state.
