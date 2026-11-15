# Full offline GPU goal — active

The objective is the complete SM8750/A830 offline chain: native C++ driver,
Elden board adapter, Metal plugin, Rust bridge and Turnip, with real ABI/link
checks, reproducible scripts, artifacts, reports and merge instructions. No
tablet operation or automatic merge into mainline is authorized by this goal.
Hardware rendering is a subsequent validation phase, not an offline claim.

## Current evidence and remaining acceptance

| Requirement | Current evidence | Still needed |
| --- | --- | --- |
| Native C++ driver | 27 production C++ core units compiled against the actual source-built OS17 kernel context; archive produced; 14 exact warning sites reviewed by source hash | Elden replacement for four SM8550-bound modules; full native link and original/native ABI bindings |
| Board adaptation | Pinned resources,14 GPU/3 GMU OPPs/four speed bins; actual HFI startup computation passed50 host cases and native compile; four public Gen8 firmware inputs hash/structure checked | Runtime resource ownership, callbacks, startup/stop/recovery and framebuffer handoff |
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
  `NativeAdrenoService` require `ShengGmu.h`. They are SM8550 integration pieces,
  not a valid Elden implementation. Replacements remain mandatory, not stubs.
- Initial patch failures were traced to CRLF in the published patch blobs,
  versus LF runtime sources. All26 A830 patches strictly apply to624bab after
  changing only line endings. The exporter now records original Git and build
  hashes separately; no source logic, patch hunk, or validation was omitted.
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
