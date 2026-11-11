# v91 boot packaging: qualified inputs, incomplete source closure

The maintained tools now assemble and sign a candidate **without using an old
boot image as its carrier**. BootShim is compiled from source; the Android header,
14,536-byte ADT, menu and trust-cache container are generated from the structured
metadata in this directory. The original code-directory list contains public
software hashes, not signing keys or executable bytes. Only the two expected
owned components are appended.

The entry stage is `tools/prepare_v91_entry.py`. It permits only the previously
qualified entry segment/branch and the one AMFI word at `0xfffffff004aaecfc`.
The 100-byte drawing body matches the device-tested v91 body. Its final branch
may be relocated as the image end changes; the return target must remain the
same. This stage is distinct from the prelink executable-unchanged check.

The accepted **UEFI FD** still prevents declaring full source closure. The
standalone EFI now rebuilds with only the permitted CodeView GUID difference;
the Data seed now rebuilds from the original OS with the same required logical
contents, ownership, permissions, volume identity and APFS feature flags. These
binary outputs are not bundled in Git. See `ASSET-REBUILD.md` and
`external-inputs.json` for the qualified contracts and remaining firmware work.

`tools/build_v91_boot.py --help` lists the explicit inputs. On Windows it can use
`--wsl-distro Ubuntu-22.04` for clang, mkfs.fat, mtools, Python/OpenSSL and avbtool.
The provided AOSP test key remains outside this repository. The exact stock
boot contract is checked, including rollback **1738713600 / footer location 0**.
The image remains 96MiB. There is no device operation in the builder.

`tools/audit_v91_boot.py` independently checks AVB and parent chain, FAT mirrors,
embedded asset hashes, memory/menu policy, entry-body identity and the relocated
return target. It performs no QEMU or hardware run. Static acceptance is not
device acceptance, nor does it complete the remaining asset-source work.

The first candidate, retaining all three historical binary inputs, passed with image SHA
`0665b77c81ae250e1465301b0f2ba12190b3caed2d6399f1b69f7bb256533177`.
The candidate using the source-built loader and Data seed also passed with SHA
`1fc119f130d2c96b5a277a0d79127eda340b0597eb99df21c696484e8645198e`.
Its session identity also binds firmware, loader, trust and Data inputs.
Neither has been flashed. The full maintenance/peripheral Goal remains active.
