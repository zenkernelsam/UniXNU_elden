# v109: 8GiB native4K memory window

This explicit profile increases the requested contiguous XNU window from6GiB
to8GiB. The unchanged DarwinBoot driver obtains a fresh EFI map and succeeds
only when AllocatePages can reserve a real conventional-memory run. It does
not assume the old13GiB map remains valid, adopt outside pages, or free any
firmware/DSP/security reservation. Data remains a separate512MiB RAM volume.

The native4K bootstrap pool grows16→24 pages. Initial8GiB identity/linear
maps require at most20 pages;24 also covers arm_vm_init's conservative bound
with image/seed,1GiB slide and block-alignment padding. The old debug check
compared page count to a byte count; this profile corrects the units only in
the native4K configuration. Inherited managed-RT Normal-NC behavior is kept.
Only pmap, arm_vm_init and start are rebuilt. The31 original Apple execution
regions (including the v108 network family) remain unchanged.

## Build and qualification

Reproduce v108 first. Supply absolute paths to the original v91 core capture,
the tested v106 RT build, the v108 build and the pinned original kernelcache.

```sh
python tools/build_memory_window_components.py --core "$CORE" --rt-build "$RT" \
  --llvm "$LLVM" --output "$COMPONENTS"
python tools/link_memory_window_kernel.py --baseline "$V108" --components "$COMPONENTS" \
  --core "$CORE" --rt-build "$RT" --prelink-repository "$PRELINK" \
  --original-kernelcache "$ORIGINAL" --output "$OUT"
python tools/audit_memory_window_kernel.py --baseline "$V108" --build "$OUT" \
  --components "$COMPONENTS" --core "$CORE" --llvm "$LLVM" --output "$OUT/kernel-audit.json"
```

Create prepare_v91_entry.py's plan from the produced kernel SHA and v91's
original_executable_sha256. Apply that existing entry/AMFI transform; pack
and audit with build_v91_boot.py/audit_v91_boot.py --hid-report "$OUT/result.json"
and the same v108 firmware/loader/Data/library/agent/stock/signing inputs.
Only the darwinboot-memory-size ADT property changes. Menu, recovery, floor
1738713600/footer0/parent3 and original trust inputs remain frozen.

The independent audit binds three literal instruction changes in the entire
9140-byte start section, proves every changed PAGE21 relocation still targets
the same named symbol, verifies the96KiB zero-initialized storage extent, and
checks all inherited inputs. The entry return relocated by0x8000; the104B
entry stub body and start_first_cpu branch semantics remain qualified.

Initial component build was rejected after audit found a nested assym.s
include restored the old16-page definitions. The final compiler overlay binds
all aliases and rejects redefinitions. Final components2 has exactly three
capacity instruction changes and no new SIMD; original start source is
unchanged. Initial audit errors confused the section origin/relative symbol
values with absolute addresses; the final audit resolves both explicitly.
All initial attempts remain in Mac lab/memory-v109-20261114 and the workbench.

## First hardware window

Signed image9c6acae1…/session8137d4147019711b booted with actual
physbase0x8c007c000, physsize0x200000000. Six complete IOKit snapshots show
busy0/providers3; all recorded SMP holds remain onlineff. Two sampled frames
passed full row/CRC checks; the second visibly shows the lock screen.13 full
copy samples have median58.61ms. Capture1,362,428B/drop17 has no recorded
panic/userfault/ENOSPC markers. Typed sync/reset and exact stock restoration
completed; Android/root/green and sleep confirmed.

This is not a full-page memory stress test, Settings trial,16GiB claim or
new physical-finger acceptance. A late software request was rejected before
transmission because the target had already returned. Follow-up software UI
validation uses the same signed image and an automatic host action sequence.

## Software UI follow-up

The first UI run accepted swipe/tap but sampled only the white launch page
(drop22). The follow-up captured a populated Settings/General pane in frame9,
CRC b92cfd0a, with all380 rows verified. It also captured an anomalous repeated
column transition on Home and desktop placeholders; animation is not qualified.
The second Settings tap was rejected by the target's existing once-per-boot
test guard. No additional tap or target change bypassed that guard.

Frame12 has an odd-length damaged pixel row, so the strict whole-log decoder
refused the capture. Original lines were demultiplexed by frame ID and passed
individually through the unchanged decoder: frames1–11 validate, frame12 is
rejected without pixel repair. The log metadata confirms no USB reconnect
discontinuities; global dropped10 remains. The accepted frame9 is evidence
for populated Settings, not About latency or a physical-finger trial.

This follow-up logged6,089,821B, median26 full-copy samples58.68ms, then typed
sync/reset and exact stock restoration completed. All three v109 trials are
terminal and Android/root/green is restored. Compile warnings are retained:
pmap32, arm_vm_init10, start0; they are not waived or described as zero warnings.
Source and derived-input fingerprints, full native/prelink audit and hardware
records are checked in here; all binary/stock assets remain in the workbench.
