# Elden GPU offline integration

The **complete offline build was delivered on2026-11-16**. Read
[GPU-GOAL-STATUS.md](GPU-GOAL-STATUS.md) for its evidence and qualification
boundaries. This document retains the earlier component-stage record.

The owner authorized a separate GPU workstream on 2026-11-15 while the main
workstream continues UFS and peripheral integration. This branch does not enable
the GPU, change a boot profile, access the tablet, or alter the mainline sequence.

## Checkout and merge boundary

- Mainline: `D:\Desktop\Git\UniXNU_elden`, branch `main`.
- GPU: `C:\Users\Cisco He\.codex\worktrees\elden-gpu-offline\UniXNU_elden`,
  branch `codex/elden-gpu-offline`, based on `a13e5087` (v153).
- Workbench evidence: `D:\Desktop\Git\Patch\Y700-Elden\lab`.
- This branch's commits contain the offline tools, pins and evidence only.
  Merge by reviewed commit after handoff; do not copy its entire checkout over
  mainline or import unrelated staging. Keep UFS/QRTR/CPU changes with their owner.

## Source strategy

Sheng's Metal/Turnip integration, ABI, allocation/retirement and presentation
methods remain the common reference. The closer hardware donor is the public
OnePlus 13T SM8750/A830 work, also targeting arm64/iPadOS17. An A740 register
sequence is not an A830 hardware contract.

The source lock is [gpu-offline.lock.json](../config/gpu-offline.lock.json).
The first pass deliberately used generic firmware `20ad3d7` and exposed a real
integration failure: `Sm8750GmuColdStart` consumes `performance_table` and
`AdrenoHfiPerfA8xx`, absent from that A6xx/A7xx-only header. This was not fixed
with a fake constant or synthetic success. The published A830 branches contain
the matching implementation:

| Component | Repository / selected branch | Exact revision |
| --- | --- | --- |
| Kernel GPU source | IssacL04/Adreno740 / a830-sm8750 | `9c34ea46aa991c4fed6f0347a814473ad626de77` |
| Metal frontend | IssacL04/AdrenoMetal / a830-sm8750 | `a4aa33c84fa6b0971374006d30edb436966a4801` |
| Firmware/HFI library | IssacL04/AdrenoFirmware / a830-sm8750 | `505d0a1ac079339ee1486f0e19cfd8bd36a282a2` |
| SM8750 platform | IssacL04/UniXNU-sm8750-common / main | `feaa135df4763e3c19a155402e6b17b124a12e13` |
| PM4 | UniXNU/AdrenoPM4 / main | `282606b528b2595bc4274ea7490b62b323f35a5f` |
| SMMU helpers | UniXNU/ArmSmmu4K / main | `83969cd874503e65997ff7639f7dcf3d0a92cd17` |

The existing A830 reference directories contain uncommitted modifications and
untracked files. Do not equate their working contents with `HEAD`: relevant
extensions are published on `a830-sm8750`, not on default `main`. The builder
exports immutable Git blobs, bypassing all local modifications. The additional
`AdrenoFirmware-a830` clone retains its main checkout; its feature ref is fetched
and exported explicitly. There are no copied upstream implementation edits or
downstream driver patches in this first component package.

Before preparation, all 32 public UniXNU repositories' default branches were
checked, including the two documentation/virtual repositories without local
clones. Standard sync fast-forwarded three clones, left 21 current and five
detached, and retained the deleted Adreno740 tracking-branch error; a subsequent
explicit default-ref fetch completed that repository's review. Relevant updates
were AdrenoMetal reflected priming inputs, UniXNU AIR/priming/recovery and CI
handoff documentation, and Sheng runtime-evidence/macOS qualification notes.
All four existing A830 companion fork defaults and the three selected feature
refs were checked separately. Source snapshot and original errors are retained
under `lab/reference-reviews/20261115-gpu-offline-upstream`.

## Build and update workflow

Use Python 3.12 for preparation on Windows. On Linux/macOS, the checks need
Python, Clang/Clang++, the host ASan/UBSan runtime, llvm-objdump and llvm-nm.
The first run used Ubuntu-22.04 WSL with Clang14. Cross compilation of the C
components does not require a fake XNU SDK; Clang's freestanding headers suffice.
The C++ host test uses the upstream IOKit doubles and is not native ABI proof.

```powershell
py -3.12 tools/build_gpu_offline.py prepare `
  --references D:\Desktop\Git\Patch\Y700-Elden\research `
  --output "$env:TEMP\elden-gpu-build-new"
```

Run the matching check on that exported directory using Linux paths (or native
macOS paths); each output/check directory must be new so failures are retained:

```sh
python3 tools/build_gpu_offline.py check --output /path/to/elden-gpu-build-new \
  --objdump llvm-objdump-14 --nm llvm-nm-14
```

To follow later upstream fixes:

```powershell
py -3.12 tools/review_gpu_update.py `
  --references D:\Desktop\Git\Patch\Y700-Elden\research `
  --fetch-selected --output "$env:TEMP\elden-gpu-update-new"
```

This fetches the **selected named branches**, keeps checkout/staging unchanged,
records commit and file-stat differences, flags changes to `NativeAdrenoABI.h`,
and refuses a firmware proposal without the A8xx fields. It writes `proposal.json`
without changing the active lock. These are interface checks, not proof of binary
or runtime compatibility. Review the actual changed code, then run both prepare
and check with `--lock /path/to/proposal.json` in a fresh output. Only commit a new
lock after the affected tests and any necessary native ABI/link checks pass.

The organization snapshot is dated evidence, not an instruction to move every
dependency to its default HEAD. The six-hour reference sync continues to maintain
upstream references; this deliberate proposal workflow upgrades GPU inputs.
If a fix is merged upstream, retire the corresponding compatibility branch or
patch only after verifying equivalence. Do not retain a private copied driver
tree as the long-term update mechanism.

## Current qualification boundary

First complete run: **seven host suites and eleven native C objects passed**.
The exact outputs, undefined symbols and source-manifest hash are in
[the build record](evidence/gpu-offline-build-20261115.json). Five mutations of
an actual object header were rejected (magic, CPU, file kind, truncated command
table and wrong OS version). This validates the artifact checker, not hardware.

The first component package covers existing SM8750 clock/power sequencing, ZAP
callback protocol, SoC decoding, HFI transport/startup, PM4 encoding and A830
protected initialization through software peers. Native C objects are checked
for ordinary ARM64 Mach-O/iOS17 load metadata, absent automatic initializers and
scalar instructions. Their unresolved imports are recorded for later linking.

`test_votes.c` requires an external pinned Linux `source_arc`/`source_bandwidth`
oracle. Its attempted link failed because that oracle was absent; that suite is
explicitly unrun, not replaced with a stub or claimed to pass. The vote component
is compiled, but its independent arithmetic differential acceptance remains open.
The original compiler/link failures and the corrected raw-byte/SIMD disassembly
checker failure are retained with the workbench records.

No native C++ GPU driver/kernel link, Metal bundle, Rust bridge, Mesa/Turnip
library, firmware execution or hardware rendering is claimed by this package.
The success result is an offline prerequisite, not a flashable artifact.

## Y700-specific layer and next stages

[Pinned board evidence](evidence/gpu-board-inputs-20261115.json) reads the exact
TB322FC Linux `6f97250e` selected by the ArmadaOS lineage. It keeps source text and
hashes separate from runnable inputs. In particular, the base DTS compatible
`adreno-44050000` is not itself a measured chip-id of the tablet; the donor's
`0x44050001` gate must be reconciled with stock/runtime evidence before enabling.

| Stage | Work | Completion evidence |
| --- | --- | --- |
| A | Reproducible source/component checks | Pinned Git blobs, original host suites, C native objects and reports |
| B | Native C++ driver and userland build | Exact current OS17 XNU headers/imports, A830 archive, Metal/Rust/Turnip builds, shared ABI and signatures |
| C | Elden board adapter | Actual firmware hashes, reservations, chip revision, cmd-db rail/BCM data, speed-bin/thermal limits, SMMU ownership, bounded callbacks |
| D | Integration candidate | Explicit disabled-by-default option; full linker/prelink/Apple-range/initializer checks; existing framebuffer fallback and UFS/USB/SDAM/watchdog recovery retained |
| E | Later coordinated hardware window | Approved candidate, real power/GMU/CP/fence success, known shader pixels and teardown before SpringBoard performance claims |

Keep Elden's board data and lifecycle callbacks separate from the reusable
driver/Metal code. Do not copy the donor's 512MiB GPU pool, CB selection, 525MHz
policy or firmware identity just because both devices use SM8750. GPU test code
must not take ownership of the mainline device or shared UFS during this offline
workstream. No new automation or cross-chat message was created by this task.
