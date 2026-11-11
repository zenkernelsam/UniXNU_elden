# v91 XNU object build

This profile closes the **1,010 XNU object** portion of v91's build. It contains
119 factored compiler templates and 3,259 input providers:

- 2,960 inputs match the XNU tree reconstructed from the pinned Git commit and
  the device source overlays.
- 298 generated/configuration sources are included here with hashes and origin
  records. These are source files, not old objects or archives.
- The IconServices payload header is supplied externally and regenerated using
  the maintained helper. No original executable bytes are bundled here.

Clang's standard headers come from the installed toolchain and are hash-checked.
The verified build used Apple clang 17.0.0 with the existing Xcode installation.

From the repository root on macOS, after preparing the source tree and payload
header as described in `docs/REPRODUCIBILITY.md`:

```sh
python3 tools/build_v91_xnu_objects.py \
  --xnu-source /path/to/reconstructed/xnu \
  --xnu-repository /path/to/pinned-xnu-git \
  --payload-header /path/to/agent-output/EldenIconCpuPayload.h \
  --output /path/to/new-object-output
```

The historical names in the plan are **virtual compiler paths**. The builder
does not require those directories to exist. C/C++ compilation uses a closed
Clang virtual filesystem so includes cannot fall back to the old workbench.
Original file spellings preserve `__FILE__` strings and compiler string pooling.
Assembly uses the isolated source tree. The pinned XNU `setsegname` host tool is
built from source and applies XNU's HIB, KLD and LAST segment rules.

Every object must match its recorded loadable section bytes, sizes and flags.
DWARF and object-local addresses are excluded; this check does **not** prove the
final relocation graph, native kernel or boot image. The full portable entrypoint
has been tested in a new directory with all 1,010 objects accepted and zero old
objects copied. Native archives, final linking, prelink and boot packaging still
require their own complete reproduction checks.
