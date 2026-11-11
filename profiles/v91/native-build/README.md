# v91 native archives

This frozen profile builds 608 objects into 14 project archives, including the
kernel logging library referenced implicitly by `-lfirehose_kernel`.
It contains 32 compiler templates and 3,402 hashed source providers. Original
object/archive files are not required or copied.

Providers use the frozen repository snapshots, reconstructed XNU sources, the
four pinned dependencies in `plan.json`, and the externally regenerated NT36536
firmware header. Extra source files are included under `extra/`. No original
firmware bytes are included in the profile.

Supply Git checkouts named `corecrypto-2023`, `libpthread`, `libdispatch` and
`libplatform` under a dependency directory. They must contain the exact commits
listed in `plan.json`; the builder reads Git blobs from those commits without
changing the checkouts. The corecrypto dependency retains the historical
third-party source snapshot; it is not represented as an authenticated Apple
release archive.

```sh
python3 tools/build_v91_native_archives.py \
  --xnu-source /path/to/reconstructed/xnu \
  --xnu-repository /path/to/pinned-xnu-git \
  --dependencies /path/to/dependency-checkouts \
  --nvt-header /path/to/generated/EldenNvtFirmware.h \
  --payload-header /path/to/generated/EldenIconCpuPayload.h \
  --llvm-objcopy /path/to/llvm-20/bin/llvm-objcopy \
  --output /path/to/new-native-output
```

All 608 objects and all 14 archives passed in a fresh output directory. Checks
cover loaded section bytes, sizes and types; global/absolute symbol bindings;
and archive export/import symbol sets. Original archive-file hashes need not
match because archive/debug metadata differs. This stage does not test hardware.
