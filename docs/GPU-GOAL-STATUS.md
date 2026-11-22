# Complete offline GPU build — delivered

The Y700 SM8750/A830 offline chain completed on 2026-11-16 at source revision
`9fca00698102d4631941a5a5d3743fbf2d212b66`. Native C++, the Elden board adapter,
Rust bridge, Turnip, Metal plugin, full native/prelink and entry preparation all
passed. The final source-built baseline is fixed at a13e5087; its28 kernel stages
completed from a new output. GPU sources and outputs share one consolidated tree.

[Build instructions](GPU-BUILD.md), [merge/activation boundaries](GPU-MERGE.md),
[complete evidence](evidence/gpu-complete-20261116.json), and
[delivery receipt](evidence/gpu-delivery-20261116.json) are authoritative.
Earlier `GPU-OFFLINE.md` and progress evidence describe the component milestones.
The next agent's offline optimization plan, CiscoMac worktrees, and mainline
acceptance order are in [GPU-OPTIMIZATION-HANDOFF.md](GPU-OPTIMIZATION-HANDOFF.md).

## Results

| Item | Evidence |
| --- | --- |
| Native driver | 27 core C++ units plus runtime/framebuffer/votes/firmware providers; complete link with30 original Apple execution regions preserved |
| Board integration | Exact-source SM8750 adapter; Elden14GPU/3GMU OPPs; GPUCC0x9800, ACTLR0x303, shared RPMh owner, opt-in startup and bounded recovery drain |
| GPU ABI checks | Original IOGPU entry hashes and C2/D2 targets,136-byte instance,168-slot vtable/seven owned overrides, late initializers and required providers checked |
| Lifetime tests | Actual call gate, drain, runtime and presenter methods; concurrency, cancellation, owner replacement, mapping failure and retained-resource behavior |
| Rust | ARM64/iOS17 static archive222474240B, SHA e9f16c670b28…; all26 UniXNU patches and frozen Cargo dependencies |
| Turnip | ARM64/iOS17 dylib17945576B, SHA a6b44b1c5ba4…; exact Mesa pin and23 patches |
| Metal | A830 six-SP frontend and probe linked; bundle, Turnip and probe pass strict4KiB signatures and entitlement readback |
| Kernel | Entry-ready image54935656B, SHA6107140d70e7… |
| Delivery | 87270508B archive,37 files read back against hashes; Windows transfer SHA cef8999016da9aeb3ff69c0ca73043ad839814b58a3dfcaa352374e8069da1f1 |

The archive is at
`D:\Desktop\Git\Patch\Y700-Elden\edl_work\gpu-complete-20261116\elden-a830-offline.tar.gz`.
Full records are in the corresponding workbench `lab/records/gpu-complete-20261116`.
Mac source/outputs remain isolated in `UniXNU_elden-gpu-offline/lab/gpu-goal-20261115`;
`complete-3` is the accepted consolidated build. All build jobs are terminal.

## Qualification boundary

The supplementary **GPU-specific** ABI audit passes. The legacy prelinker's
`abi_qualified`, `loadable`, `runtime_qualified`, and
`instruction_relocation_complete` remain false; no global or hardware
qualification is inferred or relabeled. This is an offline build deliverable,
not a flashable or hardware-qualified boot image. No tablet access, deployment,
or mainline merge occurred.

GPU startup is disabled by default. The publication marker is outside the Data
overlay. Full merged boot arguments, Data/union loading, trust integration and
AVB still belong to the next coordinated hardware candidate. Cmd-db/route/chip
checks and real ZAP authentication must succeed on hardware before rendering can
be claimed. Power-off, soft restart, suspend/resume, dynamic power management,
frame rate and battery behavior remain unqualified. Shutdown retains allocations,
mappings and shared votes until reset.

Warnings remain explicit:14 reviewed core warnings; runtime3 publisher/1 memory
and5 inherited display warnings;26 baseline recovery warnings. New board runtime,
frame bridge, presenter, votes and firmware compile without warnings. Userland
Turnip retains its upstream legacy dyld representation and the recorded linker
warnings; no chained-fixup success is fabricated.

## Reproduction and corrections

The first complete attempt required explicit baseline resume; later attempts
exposed a missing LLVM PATH and Mesa selecting Apple's old Bison. Corrected
Bison3.8 selection is a private executable wrapper because `find_program` ignores
BISON. Failed directories/logs are retained. The accepted run resumes its own
hash-checked native and Rust stages; it imports no old GPU objects from another
build. The final source and build-driver hash match the published revision.

Windows Git archive/autocrlf, not published patch blobs, caused the earlier CRLF
problem. Export now reads raw Git objects and records raw/output hashes. User
confirmed the desired source is `steelbrain/metal2vulkan` **development**; the
qualified pin624bab+26 patches stays paired with UniXNU95c6788 tooling. Current
development is observed for updates but not substituted independently. The update
reviewer now covers all native/frontend/support/runtime entries and produces an
unaccepted proposal. Records are in `lab/reference-reviews/20261115-gpu-complete-review`.

The complete offline objective is fulfilled. Future hardware and mainline work
must use the handoff boundaries above; they are not included in this completion.
