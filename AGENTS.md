# UniXNU elden — authoritative source repository

Current priority is the owner's cross-host cleanup and reproducibility migration. Read the paired workbench `maintenance/IN_PROGRESS.json`; while it exists, continue that task instead of resuming driver experiments. The full peripheral Goal remains active.

All effective source/patch/tool/config updates must live in this Git repository. Do not publish proprietary firmware, original OS executables, signing secrets, device backups or generated binary payload headers. Record required external inputs by origin and full hashes; regenerate binary-derived headers.

Temporary one-off tools belong in Windows TEMP. Retired files go to Recycle Bin/Trash, never permanent deletion. WSL retired items and existing Trash are transferred recoverably to Windows Recycle Bin. Preserve unrelated projects and user edits.

Reference clones alone live in the workbench `research/`, inventoried in `config/reference-repositories.json`. Evidence/build records live outside that directory. Preserve immutable records and document relocation; do not silently rewrite their hashes.

v91 reached actual Settings content through software input. Physical touch, performance, persistent Data, charging and other peripherals are incomplete. v92 is an unsigned, untested offline framebuffer mapping experiment. Preserve both the working baseline and its recovery inputs.

Boot floor remains 1738713600 at footer0 (parent location3). Use exact device identity, fresh battery admission and complete post-write hashes. No unlock/relock/OTA, identity/RPMB/fuse writes, arbitrary charger/FET/OTG changes. Speaker attenuation/protection requires discussion; GPU last. No subagents or QEMU in this work.

Current documents are `docs/CURRENT-STATUS.md`, `docs/PLAN.md`, `docs/REPRODUCIBILITY.md` and `docs/MAINTENANCE.md`; the old docs and porting log are archived. The maintained reference sync is `tools/maintenance/sync_reference_repos.py`, which defers while the workbench maintenance marker exists.

The frozen v91 source manifest, XNU source reconstruction, owned IconServices dylib and external agent/NVT header generation are verified. The owned dylib and regenerated headers are byte-identical to the v91 inputs. Repository entrypoints now rebuild 1,010 XNU objects and 608 native objects/14 project archives without old objects. Native linking matches 50 loadable sections, their addresses/attributes, global symbols and immutable load commands. Prelink independently hashes all 30 original Apple executable regions. Artifact UUID/debug-link metadata can differ; whole kernel/image SHA equality is not claimed. See `profiles/v91/object-build`, `native-build`, `image-build` and `docs/REPRODUCIBILITY.md`.

The qualified entry/AMFI transform and carrier-independent ADT/menu/trust/FAT/signing tools now produce a statically audited candidate `0665b77c81ae250e1465301b0f2ba12190b3caed2d6399f1b69f7bb256533177`, floor 1738713600/footer0, parent boot chain3. It is unflashed. The 100-byte drawing body matches the hardware baseline; only the final return-branch encoding relocates, with the same target. Remaining maintenance: close the three pinned external inputs (accepted FD, standalone EFI, blank Data seed), validate the complete source/asset recipe, finish path migration/retirement. See `profiles/v91/boot-build/external-inputs.json`. The prelink original-executable-unchanged claim excludes the explicit AMFI single-word transform. Keep the maintenance marker and full peripheral Goal active; no hardware work or full boot source-closure claim yet.
