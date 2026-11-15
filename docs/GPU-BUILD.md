# Complete offline A830 build

This builds the Elden native adapter, A830 C++ driver, Rust bridge, Turnip and
Metal plugin together, then links the OS17 kernel and applies the existing
entry preparation. It does not create or flash a boot image. Hardware rendering
and the legacy prelinker's global ABI/loadability flags are separate acceptance
items; the supplementary GPU ABI audit does not change those flags.

## Inputs and hosts

- Python3.12 for preparation; macOS/Xcode for native and userland builds.
- Exact Git objects listed in `config/gpu-build.lock.json` and
  `config/gpu-firmware.lock.json`. `config/gpu-cargo.lock` freezes transitive
  crates. Reference working-tree edits are not build inputs.
- TB322FC Linux6f97250e, the ArmadaOS-linked **SM8750** kernel; its DTS compatible
  and OPP tables are checked. This is not the SM8650 TB321FU tree.
- A clean baseline source checkout at `a13e5087a8651b3185a0855eacf94923a21e97eb`.
  Keep it separate from the GPU source checkout and from active mainline work.
- The explicit original assets in `docs/CLEAN-INPUTS.md`. The compiler, original
  kernelcache, OS image and owned firmware are supplied in the clean input JSON.
- Rust1.98 with `aarch64-apple-ios`, LLVM tool directory from the input JSON,
  and the private prefix produced by `tools/setup_gpu_host.py` (CMake3.31.10,
  Meson1.9.1, shaderc/glslang/SPIR-V tools). No system CMake replacement.

## Preparation on Windows

Run from this GPU branch, using a **new** output directory:

```powershell
py -3.12 tools/prepare_gpu_complete.py `
  --references D:/Desktop/Git/Patch/Y700-Elden/research `
  --linux D:/Desktop/Git/Patch/Y700-Elden/research/tb322fc-linux-kernel `
  --output "$env:TEMP/elden-gpu-inputs-new"
tar -czf "$env:TEMP/elden-gpu-inputs-new.tar.gz" -C "$env:TEMP/elden-gpu-inputs-new" .
```

Transfer/extract that archive into a new Mac directory. It contains firmware and
licenses, not source-repository additions. Every exported byte is recorded in
`prepared.json`. Raw source export uses Git object reads, avoiding Windows EOL
checkout/archive filters; lock text comparison uses canonical LF.

## Complete build on macOS

```sh
python3 tools/build_gpu_complete.py \
  --prepared /path/to/prepared-inputs \
  --baseline-source /path/to/fixed-a13e508-source \
  --base-build /path/to/source-built-baseline \
  --inputs /path/to/clean-inputs.json \
  --shader-prefix /path/to/private-shader-tools \
  --output /path/to/new-complete-build
```

Optional `--mesa-cache` and `--runtime-cache` reuse only Git object databases.
Their checkouts and edits are ignored; fresh detached checkouts are patched and
compiled into the new output. No previous GPU object or Cargo target is reused.
The baseline is either built from scratch or resumed through the existing
identity/proof journal. The full native link consumes this source-built base.

The final `result.json` is written only after native compilation, real lifecycle
host tests, recovery integration, full link/prelink, GPU ABI checks, Rust, Turnip,
Metal/signatures and entry validation all finish successfully. A failed stage
retains its logs and does not emit final success. A component report alone does
not satisfy the full build. The `linked` stage's standalone report remains scoped
to that link; the top-level report is the full-chain evidence.

```sh
python3 tools/package_gpu_complete.py \
  --build /path/to/new-complete-build --output /path/to/new-delivery
```

The package includes readback-verified hashes, an inactive Data overlay and an
optional publication marker kept outside that overlay. See `GPU-MERGE.md` before
any integration. The original probe daemon is included but is not automatically
launched by these tools.

## Follow upstream without changing a qualified candidate

```powershell
py -3.12 tools/review_gpu_update.py `
  --lock config/gpu-build.lock.json `
  --references D:/Desktop/Git/Patch/Y700-Elden/research `
  --fetch-selected --output "$env:TEMP/elden-gpu-review-new"
```

Review proposed native/frontend/firmware changes together. The user's requested
metal2vulkan branch is **steelbrain/metal2vulkan development**. The built pin is
624bab plus26 UniXNU patches, paired with official95c6788 tooling metadata.
The reviewer observes the development tip but retains that compiled runtime pin;
advancing it requires the matching frontend, patch series and builder. Changes
to exact-source runtime/frame adapters deliberately fail their hash anchors until
reviewed. Repeat the complete build and ABI checks before accepting new pins.
