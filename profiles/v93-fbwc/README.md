# Framebuffer mapping candidate

This profile changes five compilation units from the source-rebuilt v91
baseline: text console, UFS-owned recovery gate, display glue, display mapping,
and a new translation checker. The remaining 1,009 XNU objects and unmodified
native objects are reused only after hash checks. v92's captured sources remain
untouched; this profile adds write-permission and virtual-span checks.

The physical target is the measured 1904×3040 BGRA framebuffer at0xfc800000,
stride7616, length23152640. The TB322FC Linux tree independently describes that
base and geometry; ArmadaOS's dynamic simplefb template is not treated as a
fixed-address guarantee. Runtime geometry must still match.

The early text-console mapping is suppressed before creation. PE_current_console
continues to return PE_state.video, and GOP kprintf still goes through the
disabled console's USB mirror. The main physmap must exclude the framebuffer.
The display creates a physical memory descriptor with write-combine cache
policy, then requests the same map option. It waits for the already qualified
independent watchdog to arm before new framebuffer access. Each page must pass
both AT S1E1R and AT S1E1W physical-address/Normal-NC checks; PAR_EL1 and interrupt
state are restored. The existing DSB before transfer retirement remains.

The new native units must contain no FP/SIMD. The old XNU text console already
contains vectorized code: its unchanged paths are compared separately, including
the known stack-slot shift and removal of an inlined vcattach copy. The new
early-return path itself must be scalar. This is not a blanket claim that every
XNU function is SIMD-free.

```sh
python3 tools/build_fbwc_kernel.py \
  --v91-core /new/v91-core --v91-native /new/v91-native --v91-image /new/v91-image \
  --xnu-repository /repos/xnu --prelink-repository /repos/XnuPrelink \
  --original-kernelcache /inputs/kernelcache.decompressed \
  --llvm /path/to/llvm-20/bin --output /new/fbwc
```

`--resume` only accepts the identical profile before archive/link creation;
failed attempts are retained. The four new exported functions are explicitly
listed. Original Apple executable extents are rehashed after prelink.

Use the ordinary entry transform, then pass `--fbwc-report /new/fbwc/result.json`
to the boot packer and auditor (the auditor also requires `--entry-report`).
The only menu argument added is bare `fbwc`, which the pinned XNU parser treats
as numeric1. This keeps the real command line within1023 bytes without removing
any existing behavior. The session identity binds the changed kernel and menu.

Hardware acceptance requires the mapping report before pixels, a complete
Settings operation with CRC-verified frames, measured copy timing and recovery.
Software input does not verify real fingers, panel-vblank behavior or60fps.
Charging, DSP policies, GPU and speaker settings are unchanged.
