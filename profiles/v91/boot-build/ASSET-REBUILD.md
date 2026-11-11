# Rebuilding the loader and initial Data volume

## Standalone EFI

`tools/build_v91_payload_loader.py` compiles the repository's loader in a new EDK
workspace. Existing firmware outputs are not reused or overwritten. It restores
the original virtual CodeView path to avoid changing section and unwind layout.
The rebuilt 31,744-byte EFI differs from the accepted file only in its CodeView
GUID: all other bytes match. `loader-contract.json` fixes the exact permitted
16-byte debug-identity range and the normalized whole-file hash; it does not
ignore executable bytes, PE attributes, unwind data or arbitrary strings.

## Initial Data seed

`tools/build_v91_data_seed.py` takes the exact original 21H461 OS image. The
`data-seed.json` manifest describes 232 OS-derived files/directories/links, the
six setup preferences and their modes. Child UID/GID values in the old entries
are historical macOS stat projections; `raw_ownership` is the authoritative
on-disk policy: anonymous `99:99` children and a `501:20` root. No old user data or host
account records are copied. Host-generated `.fseventsd` journals are excluded
from logical equivalence and are never copied from the old fixture.

The builder creates a new 512MiB unencrypted APFS image, fills it from the OS
image/declared preferences, then sets the known Data volume UUID in a **new image
copy**. Only validated APFS volume-superblock UUID fields and their Fletcher
checksums change. The source image and any tablet partitions are not modified.
It rejects encrypted, corrupt, unexpected or differently sized images. A
read-only remount verifies the UUID and every declared logical entry; read-only
`fsck_apfs` and APFS feature checks must also pass. All 233 live inode owners are
read from checksummed APFS leaves, rather than inferred from `stat()`. The packed
payload is independently checked for the same ownership. See
`../../../docs/DATA-SEED-OWNERSHIP.md` for the hardware-discovered regression.

The generated filesystem has new allocation/checkpoint/journal metadata, so its
whole-file hash differs from the historical seed. The verified rebuild preserves
the required volume UUID, OS file contents, modes, raw ownership and symlink targets.
The packed DBRDISK1 reconstruction is independently hashed against the verified
raw image. This is an offline filesystem qualification, not a tablet boot test.

```sh
python3 tools/build_v91_data_seed.py --os-image /path/to/OS.raw --output /path/to/new-seed
```

Run as UID501 (the historical fixture owner) or root; ownership operations remain
inside the newly attached test image. The packer accepts this result with
`--data-report /path/to/new-seed/result.json` and the rebuilt `data.packed`.

The accepted UEFI FD is now independently reconstructed from pinned Git objects;
see `../firmware-build/README.md`. All executable bytes, data and dependencies
match under the exact CodeView/compression contract. The full peripheral Goal
remains active; offline reconstruction is separate from device acceptance.
