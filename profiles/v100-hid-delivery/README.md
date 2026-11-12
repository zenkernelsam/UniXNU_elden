# v100 touch delivery — signed, untested

The complete v94-based image now includes the two
[HID delivery repairs](../../drivers/EldenHidDelivery/README.md). Linking,
prelink, entry transformation, signing and independent inspection passed offline.
The owner's offline-only restriction remains; this image has not been flashed.

- Image SHA256: `4a95e834ac1d4a5134881ae6561f2e93b28555e5b76ce79d5bf33c229b8baaee`
- Session: `06ee3e3f82c1cafa`
- Pre-entry kernel: `a785f1d4116b1f640622f8a3b84df975f7927fff5d75facffcc14f070a31d470`
- Entry-transformed kernel: `38cee4dbee1b5d1f4b85e58b56f3c8f0f8d2fa0f20c79e01783356da4a272058`
- Workbench image: `edl_work/hid-delivery-v100-20261112/candidate/boot-signed.img`
- Floor1738713600/footer0, parent location3;96MiB image,56MiB payload.

## Scope and evidence

Only `NativeHidInput.late.o` and `EldenHidGlue.o` change. The exact v94 core file
list and every other archive member are preserved. The profile pins the two
derived sources, loaded object sections and symbol bindings; DWARF path changes
need not produce identical whole-object digests to reproduce this code.

The first link preparation rejected four trailing newline bytes in a Darwin ar
member. The final builder checks each complete original object plus its exact
eight-byte alignment padding, then checks every new archived member again.
It does not accept arbitrary trailing bytes or strip object contents.

The result-bearing callback is at0xfffffff007a03d90 with two linked registration/
unregistration branches. Their bytes survive prelink unchanged. All30 original
Apple executable ranges are hashed unchanged. The two components are scalar and
retain the original late metaclass initialization. No other objects were recompiled.

The startup object and original entry/return addresses match v94. The existing
100-byte blue-entry body,104-byte segment contract and qualified AMFI word remain.
See `entry-assembly.json`, `entry-layout.json`, `kernel-audit.json` and `boot-audit.json`.

Menu,1023-byte primary arguments,8-second default, FD, loader, corrected RAM Data,
ADT and trust contracts remain v94. This does not merge v95 paired stores, v96
battery UI or v99 Bluetooth. FAT/assets/session/AVB checks pass. The shared auditor
also rechecked the unchanged v99 candidate and its separately qualified entry.

Static checks do not prove real-finger input or lifecycle stability. The source
faults are reproduced offline but are not proven causes of old hardware symptoms.
Service teardown/restart, sleep/wake and actual finger operation remain unverified.
All older candidates are preserved; the full peripheral Goal is incomplete.

## Reproduce

First build the v91 parents and v94 using [REPRODUCIBILITY](../../docs/REPRODUCIBILITY.md),
then both delivery objects using `tools/build_hid_delivery.py`. Do not mix the
old void callback object with the v2 provider. On macOS, from this source checkout:

```sh
python3 tools/link_hid_delivery.py \
  --v94-build /path/to/verified-v94-build \
  --delivery-build /path/to/qualified-delivery-objects \
  --prelink-repository /path/to/XnuPrelink-git \
  --original-kernelcache /path/to/21H461/kernelcache.decompressed \
  --output /path/to/new-v100-build
python3 tools/audit_hid_kernel.py \
  --build /path/to/new-v100-build --output /path/to/new-v100-build/kernel-audit.json
python3 tools/audit_entry_relocation.py --profile v100-hid-delivery \
  --baseline /path/to/verified-v94-build --build /path/to/new-v100-build \
  --core /path/to/verified-v91-core --output /path/to/new-v100-build/entry-layout.json
python3 tools/prepare_v91_entry.py \
  --kernel /path/to/new-v100-build/kernel.macho \
  --plan /path/to/new-v100-build/kernel.json --output /path/to/new-entry \
  --clang /path/to/clang
```

Follow the complete [boot recipe](../v91/boot-build/README.md), selecting this entry
output and adding `--hid-report /path/to/new-v100-build/result.json` to both
`build_v91_boot.py` and `audit_v91_boot.py`. Pass `--entry-report` to both. Do not
combine `--hid-report` with framebuffer, battery or Bluetooth experiment options.
Private original inputs and signing keys remain external. Use new output paths.

Mac final materials: project-local `lab/hid100-20261112/build2` and `entry`; the
failed archive check remains in `build`. Windows materials are in the workbench
directory above. Host/component evidence remains in `lab/records/hid-delivery-20261112`.

Once the owner lifts offline-only, one real-finger swipe/Settings/scroll-and-lift
trial can use v100, retaining v94 for a regression contrast if needed. Disable the
automatic icon injector in that window. Preserve fresh identity/battery checks,
complete write SHA and recovery. Charging, wireless and other peripherals remain
part of the full Goal; this candidate does not establish their completion.
