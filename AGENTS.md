# UniXNU elden — authoritative source repository

Cross-host cleanup and v91 source reconstruction are complete. If the paired workbench `maintenance/IN_PROGRESS.json` exists, finish its commit/receipt closure before driver experiments. Afterwards follow `docs/PLAN.md`; the full peripheral Goal remains active.

All effective source/patch/tool/config updates must live in this Git repository. Do not publish proprietary firmware, original OS executables, signing secrets, device backups or generated binary payload headers. Record required external inputs by origin and full hashes; regenerate binary-derived headers.

Temporary one-off tools belong in Windows TEMP. Retired files go to Recycle Bin/Trash, never permanent deletion. WSL retired items and existing Trash are transferred recoverably to Windows Recycle Bin. Preserve unrelated projects and user edits.

Reference clones alone live in the workbench `research/`, inventoried in `config/reference-repositories.json`. Evidence/build records live outside that directory. Preserve immutable records and document relocation; do not silently rewrite their hashes.

v91 reached actual Settings content through software input. Physical touch, performance, persistent Data, charging and other peripherals are incomplete. v92 is an unsigned, untested offline framebuffer mapping experiment. Preserve both the working baseline and its recovery inputs.

Boot floor remains 1738713600 at footer0 (parent location3). Use exact device identity, fresh battery admission and complete post-write hashes. No unlock/relock/OTA, identity/RPMB/fuse writes, arbitrary charger/FET/OTG changes. Speaker attenuation/protection requires discussion; GPU last. No subagents or QEMU in this work.

Current documents are `docs/CURRENT-STATUS.md`, `docs/PLAN.md`, `docs/REPRODUCIBILITY.md` and `docs/MAINTENANCE.md`; the old docs and porting log are archived. The maintained reference sync is `tools/maintenance/sync_reference_repos.py`, which defers while the workbench maintenance marker exists.

The frozen v91 source manifest, XNU source reconstruction, owned IconServices dylib and external agent/NVT header generation are verified. The owned dylib and regenerated headers are byte-identical to the v91 inputs. Repository entrypoints now rebuild 1,010 XNU objects and 608 native objects/14 project archives without old objects. Native linking matches 50 loadable sections, their addresses/attributes, global symbols and immutable load commands. Prelink independently hashes all 30 original Apple executable regions. Artifact UUID/debug-link metadata can differ; whole kernel/image SHA equality is not claimed. See `profiles/v91/object-build`, `native-build`, `image-build` and `docs/REPRODUCIBILITY.md`.

The rebuilt kernel, FD, loader and corrected Data seed have now passed a hardware software-swipe/Settings trial. Candidate `211b084ba1f04cfb4235e2626c8e2ee4a7d9288b96797c61743aa7282b021d40`, session `bb45c8412698e8a2`, produced the original Settings frame CRC3bd2f297. A session-bound reset-after-sync returned to fastboot and stock was restored with full SHA. The capture reported17 dropped bytes; physical touch, performance and the full peripheral Goal remain incomplete.

The first rebuilt Data seed falsely appeared ownership-equivalent through macOS stat(), but raw child inode owners were501:20 instead of the original anonymous99:99. It stayed at the boot logo; original-Data-only contrast restored Settings. The generator now uses owners-off creation and checks233 raw inode identities, and the packer independently repeats that check. The corrected source-built seed passed the same Settings operation. See docs/DATA-SEED-OWNERSHIP.md. FD SEC/FVMAIN qualification, original input requirements and the explicit entry/AMFI boundaries remain unchanged. Previous4866/1fc candidates using the bad seed are withdrawn in the workbench; do not reuse them.

447 top-level workbench source copies are archived after matching canonical hashes; seven compatibility launchers remain.328 superseded pre-v89 boot/FAT packages were moved to Windows Recycle Bin with full hashes/receipts. Keep v89-v92, recovery images, source/log evidence and backups. Historical recipes are archived, not maintained entrypoints. No builds, signers, writers or listeners remain active when the maintenance journal is closed. The next step is qualified display/physical-touch work, not declaring the full peripheral Goal complete.


Latest display result (2026-11-12): v94 framebuffer profile passed5653 read/write page checks, preserved the sampled Settings image CRC3bd2f297, and reduced17-sample full-copy median716.65→134.14ms (5.34x). Explicit sync/reset and full-SHA stock recovery passed; capture reported40 dropped bytes. See docs/FRAMEBUFFER-WC.md and profiles/v94-fbwc-observation/hardware-result.json. The owner is currently unavailable for real-finger testing and explicitly requested offline work; do not start another live test until that constraint changes. Continue offline display/input work, keeping all basic peripherals in scope.

Offline follow-up: v95-fbwc-pairs is built and signed, with469 host rectangle/fallback cases, scalar paired-store code and static audit passed. Candidate e25f1ed425308d48ac043d7488587ab9e7e60759eb14c7e06bc5b34b47dc4f79 is unflashed; performance is unknown. The source-load hypothesis was disproved (v94 already uses LDP); this candidate changes only qualified Normal-NC stores with original-path fallback. Keep v94 as the tested baseline and honor the owner's offline-only constraint.
