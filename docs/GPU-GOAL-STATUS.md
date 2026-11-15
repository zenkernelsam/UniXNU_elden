# Full offline GPU goal — active

The objective is the complete SM8750/A830 offline chain: native C++ driver,
Elden board adapter, Metal plugin, Rust bridge and Turnip, with real ABI/link
checks, reproducible scripts, artifacts, reports and merge instructions. No
tablet operation or automatic merge into mainline is authorized by this goal.
Hardware rendering is a subsequent validation phase, not an offline claim.

## Current evidence and remaining acceptance

| Requirement | Current evidence | Still needed |
| --- | --- | --- |
| Native C++ driver | 27 core units plus Elden adapter compiled; full native/prelink and GPU-specific original ABI checks pass;30 original Apple execution regions preserved | Fresh consolidated build; global legacy-emitter flags remain experimental |
| Board adaptation | Pinned resources/OPPs/firmware; shared RPMh owner; explicit one-attempt startup; bounded call/fence drain; task/epoch-owned framebuffer handoff; actual-function host tests pass | Consolidated artifact and later hardware acceptance; no power-off/soft-restart claim |
| Turnip | iOS17 ARM64 dylib built (17945576B, SHA c021a4e7…), loader exports and Mach-O target independently checked; exact Mesa85fda3b3 plus23 patches | Consolidated final build, native backend ABI pairing and plugin dependency integration |
| Rust bridge | iOS ARM64 static library built,222473344B/04fe27f1…; all26 patches applied | Final consolidated build and full native/userland ABI acceptance |
| Metal plugin | A830 six-SP topology flag, no replay, plugin/probe linked and4KiB signed; strict signatures and entitlement roundtrip passed | Final consolidated build and native framebuffer integration |
| Reproducibility | Complete Git-object exporter; source manifests; isolated Mac worktree and tool prefix | One verified complete command sequence from fresh outputs; no implicit dirty-source or prior-object dependencies |
| Upstream updates | Named-branch locks and proposal reviewer | Extend validation to the whole paired userspace/native chain |
| Delivery | Existing first-stage docs and new progress sources | Final artifact manifest, acceptance audit and reviewed merge commits |

## Active build roots

Windows source worktree remains `codex/elden-gpu-offline`. Mac now shares the
existing Git object database but has its own detached worktree at
`/Users/ciscohe/Desktop/UniXNU_elden-gpu-offline`. Mainline files/outputs were not
updated or merged. Mac work files are under `lab/gpu-goal-20261115` there.
The native context is read from the earlier source-only base build; no old GPU
object is used. `build_gpu_native.py` hashes its staged header/source inputs.

The current complete-chain source candidate is `config/gpu-build.lock.json`;
the original validated component lock remains `config/gpu-offline.lock.json`.
Turnip currently compiles under `tree-current`; the matching A830/Rust pair
compiles under `tree`. These temporary stage roots are not a substitute for the
final fresh, consolidated full-chain run. Earlier attempts remain interpretable.

## Integration findings retained

- Homebrew's installed CMake4.4.4 lacks its module directory. System CMake was
  not overwritten. `setup_gpu_host.py` built shaderc/SPIR-V/glslang in an isolated
  prefix with wheel CMake3.31.10. The unavailable3.31.8 attempt is retained.
- Windows backslashes in an exported manifest failed on macOS. The exporter now
  uses POSIX paths; source bytes did not change.
- The historical warning-group flag is unsupported by current Xcode and is
  removed from the derived command. Other warnings remain checked. The14 actual
  diagnostics are recorded in `config/gpu-native-warnings.json`, tied to source
  identity and exact sites; this is not a warning-free claim.
- `NativeAdrenoGmuConfig`, `NativeAdrenoGmuPlatform`, `NativeAdrenoKernelTime` and
  `NativeAdrenoService` bind SM8550. Their providers now come from the real
  hash-bound SM8750 runtime, adapted in `drivers/EldenGpu/RuntimeSource.py`.
  Unavailable optional diagnostics remain absent, with original refusal paths.
- Correction to the first export diagnosis: Windows Git archive/autocrlf
  converted LF Git blobs to CRLF; the published blobs were not the cause.
  The exporter now uses ls-tree and cat-file --batch, bypassing both checkout
  and archive filters. It records raw Git and output hashes separately; only
  genuinely CRLF patch blobs receive an explicit line-ending transform.
  All26 A830 patches apply strictly to624bab. Earlier reports remain historical
  evidence, not the current provenance claim.
- The active candidate retains A830 Metal `a4aa33c` and runtime624bab with the
  matching upstream bridge builder95c6788, plus current remaining tooling. The
  temporary current-Metal19bb66c/555b experiment is archived as evidence, not
  silently installed. Complete ABI/link and bundle acceptance still remain.
- User clarified that the desired metal2vulkan source is steelbrain's
  `development` branch. Official historical metadata95c6788 records624bab on
  development; current development has diverged. The branch is now explicitly
  tracked and cloned as a reference, while the compiled pin and26 UniXNU patches
  remain frozen for reproducibility. The latest tip is a coordinated-update
  candidate, not silently substituted into this build.
- The iOS17 Turnip artifact uses legacy dyld-info binding, so the upstream
  chained-fixup-only self-binding optimization is inapplicable. Its legal legacy
  representation is preserved and recorded; no binding bytes are reinterpreted.
  Plugin/Turnip/probe signing and Mach-O version validation passed afterward.
- `config/gpu-firmware.lock.json` identifies public linux-firmware SM8750 inputs.
  GMU matches the donor's published hash; SQE/AQE/ZAP differ. Their identity,
  ZAP metadata/payload structure and8KiB reservation bounds were checked, not
  on-device authentication or equality to the user's stock Android.

The goal stays active until every remaining acceptance item above is actually
verified. Source preparation, a static core archive, or successful host tests
alone cannot complete it.

## Runtime integration, 2026-11-15

`full-link-4` in the isolated Mac GPU lab linked kernel2325d974 and native9d9ef6f4.
The supplementary audit checks original IOGPU entry bytes,136-byte accelerator
instance,168 vtable slots with seven owned overrides, exact C2/D2 branch targets,
late initializers and required GPU/frame/RPMh interfaces. It does not overwrite
the legacy emitter's false ABI/loadability/relocation/runtime qualification flags.

Host tests exercise512 held concurrent calls/3200 denied entries,12 real drain
cases, and actual runtime/presenter methods for cancellation, mapping failure,
owner replacement and balanced references. These are not live GPU tests.

The adapter retains donor maximum-valid shared rail/HL0 bandwidth votes pending
a common consumer ledger. It does not lower shared power during shutdown, claim
dynamic power management, authenticate the public firmware on hardware, or
claim that all donor GPU pool/CB/chip assumptions have been observed on Y700.
Startup is opt-in and requires exact OPP, WC framebuffer and armed recovery.
GPU drain runs before existing UFS shutdown; panic/watchdog routes are unchanged.

The fresh fixed-source baseline at a13e508 has now completed all kernel stages.
`tools/build_gpu_complete.py` is the next consolidated build entrypoint; successful
individual builds do not substitute for its final artifact report.
