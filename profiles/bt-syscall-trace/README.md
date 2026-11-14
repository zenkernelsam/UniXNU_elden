# v126 socket/control observation

Two exact reconstructed XNU17 sources are instrumented: uipc_syscalls.c and
kern_control.c. The original21H461 bluetoothd name/CDHash are required; each unit
records at most32 selected invocations. Socket allocation/creation, controller
query and connect entry/return are observed. Source insertions are reversible;
return/error paths are retained.

The original C units contain SIMD, so an initial whole-unit scalar assertion
was invalid. The final builder compares per-function original vector operation
sequences, normalizing stack offsets; for ctl_ioctl only, x19→x20 is the compiler's
reallocation of its existing ctl_info pointer. Its12 original copy operations
remain. The new noinline identity helper has no vector instructions. Its two API
output arrays skip compiler auto-zero, initialize the name endpoints explicitly,
and read the digest only on successful proc_getcdhash. An attempted no-neon
function attribute did not remove auto-zero vector stores and was not retained.

The actual helper passes Clang ASan/UBSan failed-name/hash-output, wrong identity,
null process and32-event-bound checks. WSL's older GCC rejects the uninitialized
attribute; that failed host attempt is preserved. Native compilation retains
original XNU warnings; it is not warning-free. Final Mac build is syscall-v126-4.
Only two filelist objects change; all project archives and30 original Apple
execution regions are verified. Mode2/runtime/Data/menu/AVB remain unchanged.

Clean pipeline: add `--bt-syscall-trace` after `--bt-exit-stack
--software-keystore --bluetool-launch --bt-transport-trace`. Every stage has been
executed separately; the combined full empty-output invocation is not rerun.
Direct tool is `tools/build_bt_syscall_trace.py`, whose parents are repository
source-build outputs, not historical v99 binaries.

Hardware: boot9eb4a7ac781f1053529170a69c38f1640317d2a94f64240a0903bddb5c8943d0,
sessionf562b9d228676196. Two bootstrap reads again return Ready/0; no selected
socket/control marker is captured before USB timeout.110923B/drop0 before the
discontinuity does not prove the later calls never executed. Automatic physical
fastboot/full stock restore and Android/root/green pass; reset cause is unknown,
not a proven sync return. This remains a diagnostic candidate.

The subsequent exact OS-cache inspection confirms the original BlueTool job
still lacks Sheng's lifecycle overrides. Next complete that known integration
difference before adding finer syscall probes. See
[BLUETOOL-LIFECYCLE](../../docs/BLUETOOL-LIFECYCLE.md).
