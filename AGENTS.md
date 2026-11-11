# UniXNU elden — authoritative source repository

Current priority is the owner's cross-host cleanup and reproducibility migration. Read the paired workbench `maintenance/IN_PROGRESS.json`; while it exists, continue that task instead of resuming driver experiments. The full peripheral Goal remains active.

All effective source/patch/tool/config updates must live in this Git repository. Do not publish proprietary firmware, original OS executables, signing secrets, device backups or generated binary payload headers. Record required external inputs by origin and full hashes; regenerate binary-derived headers.

Temporary one-off tools belong in Windows TEMP. Retired files go to Recycle Bin/Trash, never permanent deletion. WSL retired items and existing Trash are transferred recoverably to Windows Recycle Bin. Preserve unrelated projects and user edits.

Reference clones alone live in the workbench `research/`, inventoried in `config/reference-repositories.json`. Evidence/build records live outside that directory. Preserve immutable records and document relocation; do not silently rewrite their hashes.

v91 reached actual Settings content through software input. Physical touch, performance, persistent Data, charging and other peripherals are incomplete. v92 is an unsigned, untested offline framebuffer mapping experiment. Preserve both the working baseline and its recovery inputs.

Boot floor remains 1738713600 at footer0 (parent location3). Use exact device identity, fresh battery admission and complete post-write hashes. No unlock/relock/OTA, identity/RPMB/fuse writes, arbitrary charger/FET/OTG changes. Speaker attenuation/protection requires discussion; GPU last. No subagents or QEMU in this work.

Current documents are `docs/CURRENT-STATUS.md`, `docs/PLAN.md`, `docs/REPRODUCIBILITY.md` and `docs/MAINTENANCE.md`; the old docs and porting log are archived. The maintained reference sync is `tools/maintenance/sync_reference_repos.py`, which defers while the workbench maintenance marker exists.

The frozen v91 source manifest, clean XNU source reconstruction, owned IconServices dylib build and regenerated external agent payload header have been verified. The dylib and generated header are byte-identical to the v91 device inputs. Full clean native archive/XNU/prelink/boot reconstruction and active tool path migration remain unfinished. Keep the maintenance marker and full Goal active until those remaining steps are done; do not claim full clean-clone reproducibility from the component results.
