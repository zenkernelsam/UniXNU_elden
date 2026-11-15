# Elden GPU handoff and merge boundary

Work only from `codex/elden-gpu-offline`. Its base is a13e5087; mainline's later
UFS/QRTR/Wi-Fi/CPU work is not part of this branch. Review the commits after that
base and cherry-pick them in order, or perform a reviewed merge. Do not copy the
whole GPU checkout over mainline. No merge or tablet operation was performed by
this offline task. Resolve AGENTS/status documentation by retaining both owners'
latest evidence, not by replacing mainline's history.

## Integration points

- `RuntimeSource.py` derives the genuine SM8750 GMU/SMMU/CP/ZAP implementation
  from pinned Pagani source. It uses Elden OPPs, GPUCC0x9800 and ACTLR0x303, and
  the existing shared RPMh writer. Do not introduce a second DRV2 owner.
- `FrameSource.py` adapts the current Elden display glue and pinned GopFramebuffer
  presenter. Failed offers retain CPU fallback. Task/epoch checks protect the
  actual owner, including close during mapping and replacement by another task.
- `build_gpu_recovery.py` adds a bounded GPU drain **before** UFS wait shutdown.
  Newer mainline UFS code must be regenerated from its current source, retaining
  that order. Never replace its entire archive with the old GPU build's archive.
- GPU/GOP metaclass initializers stay late; original IOGPU parent bindings and
  entry hashes are checked. Required native providers are linked without general
  unresolved-symbol allowances. Unavailable optional diagnostics remain absent.

## Activation is a subsequent coordinated integration

Default startup has no `elden-gpu=1` and makes no GPU attempt. To prepare a later
test, choose a valid Y700 OPP and use `elden-gpu=1 elden-gpu-hz=<Hz>`. The adapter
also requires WC framebuffer mode2 (`fbwc=1` in the qualified display path) and
armed reboot/RPC admission. One boot permits one startup attempt. Its10s startup
access budget and2s requested drain budget are not proof against an unreturning
hardware bus transaction; existing hardware watchdog protection remains essential.

The delivery's `rootfs/private/var/ngpu/addons/NativeAdrenoMetal.bundle` must be
exposed at `/System/Library/Extensions/NativeAdrenoMetal.bundle` through the
existing reviewed RAM Data/union mechanism. Merge its three signed CDHashes into
the actual candidate trust cache. The optional
`activation/NativeAdrenoMetal.publish` goes **beside** the bundle in that union
view only when intentionally enabling publication. Leave the marker absent for
probe-only preparation. Do not edit sealed bundle files after signing.

`rootfs/private/var/ngpu/tu_debug` contains `sysmem,nocb`, using the plugin's real
file reader. This follows the A830 donor's conservative color workaround while
GMEM remains unqualified. No whole-system environment override is required.
The optional probe executable/job is supplied; launching it or integrating its
job cache is a separate explicit choice, not automatic boot configuration.

Existing baseline boot args can already reach1023 bytes. Do not append the GPU
fragment blindly or truncate recovery/identity arguments to make room. The final
merged profile must choose which temporary observation flags to retire, retain
the actual recovery/power gates, and pass its own complete menu/entry/trust/AVB
audit. This deliverable is an entry-ready kernel plus userland, not a flashable
or hardware-qualified boot candidate.

## Remaining hardware facts and policy limits

Public firmware is hash/structure checked, not authenticated on this tablet or
asserted equal to Android stock. The runtime retains genuine donor validation
and refuses failed chip/route/startup checks. Cmd-db contents, live SMMU ownership,
ZAP authentication, real OPP/thermal suitability and shader pixels still require
the first coordinated hardware window. No observed frame rate is claimed.

Shared rails/bandwidth retain the donor maximum-valid policy pending a common
consumer ledger; this is not a power-saving implementation. Shutdown closes new
calls and drains existing fences; allocations, mappings and votes remain until
platform reset. It does not claim soft restart, GPU power-off, suspend/resume or
complete device-wide teardown. GPU-specific ABI checks do not override the
legacy prelinker's experimental global ABI/loadability flags.
