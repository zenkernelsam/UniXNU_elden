# v106 managed real-time memory fixes Settings reload

On v105, a real Settings click made backboardd fault on `DC ZVA x3` three times.
The exact21H461 cache bytes and UUID-matched local symbols identify the caller
as `CA::OGL::SWContext::set_surface`. The fault syndrome is an alignment fault;
the current XNU RT policy uses Device memory. Sheng's newer port maps RT memory
as Normal NC. This adaptation limits that change to `pa_valid()` managed DRAM
inside `wimg_to_pte`; MMIO, other memory types, execute permissions and the
original cache/TLB synchronization stay unchanged. It does not alter RAM size.

`rt_memory_patch.py` derives the single case from the exact frozen pmap source.
9,216 comparisons of the actual old/new function bodies exercise every8-bit
memory type, upper flags and managed-range boundaries. They use fixture constants
and range classification, so are not a hardware page-table test. The final native
unit retains all173 pre-existing vector instructions exactly; the modified
304-byte helper is scalar. The original failed blanket no-vector check is retained
in the build logs. Only pmap is recompiled,13 linked archives are unchanged, and
the helper bytes survive prelink. All30 original Apple execution ranges match.

Main candidate `0b34db586cd05e493df620d83d1f5244f2cd520646c20b930395b84a6bc53dae`,
session `f41202ebebeb5e63`. The user opened Settings and General, reporting
improvement and no SpringBoard reload. No fault/ENOSPC marker occurred in the
capture, with24 dropped bytes. About initially appeared stuck. Full copy median
60.20ms; physical reports and orderly shutdown are recorded. Original assets,
v105 menu,6GiB memory,512MiB RAM Data and rollback floor are preserved.

The optional `--wait-sample` variant uses the **same kernel** and the already
compiled process-wait sampler. It replaces `elden-ui` with `elden-user-wait`,
compacting the two remaining numeric-one battery flags to fit1023 bytes. The
runtime-observer selector stays enabled but the PC sends no synthetic input.
Image `ea11a8700f71629d392a944654781a36b1b71e917b541fa14609095937fbab5d`,
session `c4efa0a7a7978cbd`, captured two matching Preferences waits in the About
MAC-address query, blocked on SystemConfiguration's IOKit-quiescence initializer.
Passes3/4 left that chain and the user confirmed About displayed. That capture
has28 dropped bytes. Both runs returned via sync RPC and restored stock with
full-image SHA. This establishes functional progress, not all-driver completion.

Reproduction: run `build_rt_memory_kernel.py` with the qualified v105 `--baseline`,
v91 `--core`, fixed `--prelink-repository`, original `--original-kernelcache`,
LLVM and a new output. Run `audit_rt_memory_kernel.py` with those inputs,
`audit_entry_relocation.py --profile v106-rt-memory`, and `prepare_v91_entry.py`.
Use the normal v105 boot recipe with this build's `--hid-report`; add
`--wait-sample` to both packer and auditor only for the diagnostic variant.
`test_rt_memory_patch.py --source ORIGINAL_PMAP_C --output NEW_DIRECTORY`
repeats the host comparisons. Builds run on macOS; original Apple inputs stay private.

The fault page's attributes were not read back before reboot, so retain that
limit on the original diagnosis. About latency needs the missing/busy IOKit
service identified; do not fake network readiness or apply the macOS AES fix
without confirming the iPadOS17 provider. A larger RAM Data and expansion beyond
6GiB are separate next steps, using actual free-memory/reservation evidence.
