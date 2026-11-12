# v97 WCN build profile — offline, unbooted

Reproduce this exact profile at commit `94b6d8c219d792c88c903fc17e178ef819c6627c`.
Later development changes the owned WCN sources; the v97 hash guard deliberately
refuses those changed inputs. The next bootstrap-status work uses v98 explicitly.

This profile compiles the real WCN stack and connects its existing startup and
new shutdown paths to the tested v94 display/recovery baseline. It is a kernel
build profile, not a flashable or hardware-qualified boot candidate.

`plan.json` pins23 owned files,20 dependency files at Pagani commit
`c888efd3ce1b817d13f28783a77cf689cb2255c9`, and the five exact TB322FC firmware
assets. The reference repository is supplied explicitly; working-tree edits
and newly synchronized branches are not used. Firmware bytes are external
inputs and are regenerated into one shared C definition, never checked in.

`BridgeSource.py` reproduces the existing scoped bridge edits from original Git
blobs, then applies `BridgeCompletion.py`. The resulting C SHA remains
`7fddbf007f66d1055dd463432358708c5db4c8f743bab5a4985e2b88d86dd571`.
The prior qualified RPMh ownership fence is also reproduced and hash checked.
No new regulator voltage, GPIO encoding, HCI payload or stand-in mode is selected.

The only rebuilt existing objects are ShengUfs (weak stop hook) and
EldenDisplayGlue (existing WCN/inspector startup calls enabled at compile time).
Recovery source is derived from v94's frozen parent by adding that hook only;
the generic template's unrelated DC-native draft is not imported. All other
baseline objects/archives are reused with their hashes checked.

The19 added objects and two changed objects pass scalar code checks. New
objects have no automatic initializer; the existing UFS initializer table keeps
its size, flags and symbolic targets. The first build stopped at an overly
broad no-initializer check; its outputs remain archived. The final check
compares the existing table rather than removing it or allowing new entries.

## Result and boundaries

- Kernel SHA256: `418e7d0ccdd8040b4f198d934bc1e01d028f72d4ec327600f17943bd43509b3d`
- Native SHA256: `746fd6e045c2cfe8dcc833a1b191a402b134fa26e1a3212578a1be36b678d4cd`
- Native link and prelink passed; all30 original Apple executable ranges were hashed unchanged.
- The independent audit checks actual linked B/BL targets for startup/stop,
  confirms those call/provider bytes remain in the prelinked kernel, and
  confirms one shared AOSS owner and one GCC owner symbol.
- All five firmware payloads occur exactly once, in full, in the final kernel.

See `static-audit.json`. Windows output is in the paired workbench's
`edl_work/wcn-v97-20261112/`; Mac output is in the project-local
`lab/wcn-v97-20261112/build2/`. No entry/AMFI transformation, boot packaging,
signature, runtime option selection or hardware operation was performed.
The WCN entry remains runtime-default-off. Previous v94/v95/v96 boot artifacts
are unchanged. Do not flash this raw prelinked kernel as a boot image.

Actual WCN power/reset/UART/HCI, the exact21H461 daemon/launchd setup, pairing,
persistent keys and sleep are still pending. Shared symbol identity is not a
proof of electrical sequencing or concurrent hardware behavior. GPU and audio
remain outside this profile's enabled functions.

## Reproduce

First rebuild the v91 parents and v94 as described in the main reproducibility
documents. Preserve those output directories while creating this child build.
On macOS with the same Xcode/iPhoneOS environment and Python3.12:

```sh
python3 tools/build_wcn_kernel.py \
  --v94-build /path/to/verified-v94-build \
  --dependency /path/to/UniXNU-Pagani-Drivers-git \
  --firmware /path/to/verified-five-firmware-files \
  --xnu-source /path/to/reconstructed-xnu \
  --prelink-repository /path/to/XnuPrelink-git \
  --original-kernelcache /path/to/21H461/kernelcache.decompressed \
  --llvm /path/to/llvm/bin --output /path/to/new-v97-build
python3 tools/audit_wcn_kernel.py \
  --build /path/to/new-v97-build \
  --firmware /path/to/verified-five-firmware-files \
  --llvm /path/to/llvm/bin --output /path/to/new-v97-build/independent-audit.json
```

Owned file hashes are checked before compilation. The C++ units reuse the
parent's closed VFS/compile context; uncaptured kernel-control headers come
from the supplied reconstructed XNU and are recorded individually. Baseline
initializers and original Apple executable ranges retain their separate checks.
