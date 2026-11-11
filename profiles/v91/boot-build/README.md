# v91 boot packaging: source-rebuilt and qualified inputs

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

The **UEFI FD** now rebuilds from pinned Git objects and passes the complete
SEC/FVMAIN structural contract. The standalone EFI rebuilds with only the permitted CodeView GUID difference;
the Data seed now rebuilds from the original OS with the same required logical
contents, ownership, permissions, volume identity and APFS feature flags. These
binary outputs are not bundled in Git. See `ASSET-REBUILD.md` and
`external-inputs.json` for the qualified contracts, and `../firmware-build/` for
the firmware recipe. Proprietary upstream dependencies remain explicit inputs.

`tools/build_v91_boot.py --help` lists the explicit inputs. On Windows it can use
`--wsl-distro Ubuntu-22.04` for clang, mkfs.fat, mtools, Python/OpenSSL and avbtool.
The provided AOSP test key remains outside this repository. The exact stock
boot contract is checked, including rollback **1738713600 / footer location 0**.
The image remains 96MiB. There is no device operation in the builder.

`tools/audit_v91_boot.py` independently checks AVB and parent chain, FAT mirrors,
embedded asset hashes, memory/menu policy, entry-body identity and the relocated
return target. It performs no QEMU or hardware run. Static acceptance is not
device acceptance. The optional old baseline kernel can be replaced by the
checked-in entry-body fingerprint; private old build outputs are unnecessary.

The first candidate, retaining all three historical binary inputs, passed with image SHA
`0665b77c81ae250e1465301b0f2ba12190b3caed2d6399f1b69f7bb256533177`.
The candidate using the source-built loader and Data seed also passed with SHA
`1fc119f130d2c96b5a277a0d79127eda340b0597eb99df21c696484e8645198e`.
Its session identity also binds firmware, loader, trust and Data inputs.
Neither has been flashed. The full maintenance/peripheral Goal remains active.

The final candidate with the independently rebuilt FD, loader, Data and kernel
passed all static checks with SHA
`4866c181ffb9c4d927e3d4c4cc3833110d80853288947109e03c0566c646a9b5`,
session `3cf1627c47aa4365`. Its subsequent hardware run exposed the anonymous
Data ownership mismatch and it is superseded; the static record remains in
`../validation/source-closed-candidate.json` for the result.

The corrected source-built candidate is
`211b084ba1f04cfb4235e2626c8e2ee4a7d9288b96797c61743aa7282b021d40`,
session `bb45c8412698e8a2`. It passed the software swipe → populated Settings
operation and explicit sync/reset to fastboot; stock was restored afterwards.
See `../validation/anonymous-data-candidate.json` and
`../validation/source-rebuilt-hardware.json`. Real finger input remains untested.

```sh
python3 tools/prepare_v91_entry.py --kernel /new/prelink/kernel.macho \
  --plan /new/prelink/kernel.json --output /new/entry --clang /path/to/clang
python3 tools/build_v91_boot.py \
  --kernel /new/entry/kernel.macho --entry-report /new/entry/result.json \
  --fd /new/firmware/UEFI.fd --loader /new/loader/EldenPayloadLoader.efi \
  --data /new/seed/data.packed --data-report /new/seed/result.json \
  --library /new/icon/EldenIconCpuMode.dylib --agent /new/agent/iconservicesagent \
  --stock-boot /inputs/stock-boot-signed.img --avbtool /inputs/avbtool.py \
  --key /inputs/testkey_rsa4096.pem --clang /path/to/clang --output /new/boot
python3 tools/audit_v91_boot.py --image /new/boot/boot-signed.img \
  --entry-kernel /new/entry/kernel.macho --stock-boot /inputs/stock-boot-signed.img \
  --parent-vbmeta /inputs/stock-vbmeta-signed.img \
  --data-report /new/seed/result.json --output /new/boot/audit.json
```

Use the Data seed's successful filesystem qualification report; its builder and
independent verifier bind the packed and raw hashes to the logical manifest.
On Windows add `--wsl-distro Ubuntu-22.04` to the packaging command and use local
Windows input paths. Never substitute newer firmware/signing floors or reference
commits merely because they are available.
