# Data seed ownership regression and qualification

The first source-built seed had the same 232 declared logical entries, modes,
contents, links, volume UUID and APFS feature flags as the accepted seed. It
passed read-only fsck. Nevertheless, its first hardware run stayed at the boot
logo during the protected window. Replacing only `data.packed` with the original
seed restored the software swipe → Settings operation with the rebuilt kernel,
FD and loader. No driver or power control changed between those two runs.

The missed distinction was the inode owner stored on disk. macOS presents an
anonymous APFS owner (`99:99`) using the caller's UID/GID, even in the observed
owners-on read-only comparison. `stat()` therefore presented both old and new
trees as `501:20`. The original seed was populated with ownership ignored and
kept anonymous child owners; the first reconstruction explicitly chowned every
child to the projected host IDs. Those are different on-disk semantics.

The native Data mount in the frozen `kern_exec.c` sets anonymous UID/GID 99 in
its APFS mount arguments. The new generator follows the original owners-off
creation behavior and no longer chowns children to the Mac user's IDs. The
volume root remains `501:20`, matching the original raw inode. Modes, bytes,
links and required volume identity are still checked separately.

`tools/seed_ownership.py` reads checksummed filesystem-tree leaves of this small,
unencrypted fixture. It checks the owners for all 233 live inode identities
(root plus the 232 declared entries), including retained versions. The packer
independently repeats this check from the packed bytes. Missing identities,
ambiguous owners and a forged host-owner projection are rejected. This is a
narrow fixture verifier, not a general APFS parser or a tool to edit user data.

Five regression checks cover valid anonymous ownership, valid-checksum fixed
host ownership, conflicting retained versions, corrupt node evidence and
duplicate live inode identities. The original unsuccessful candidate and logs
are retained; their old logical checks must not be presented as proof of
functional equivalence. Physical touch and the full peripheral Goal remain
separate requirements.

The corrected source-built seed subsequently passed the protected hardware
trial: automatic swipe and Settings tap produced the same CRC-verified frame
`3bd2f297` as the original seed and the v91 baseline. A session-bound fastboot
request completed after sync, and stock Android was restored with a full image
hash match. The capture reported 17 dropped bytes; the verified frame and reset
markers were complete. This establishes this software operation, not physical
touch, performance or long-term persistence.

| Candidate | Seed | Hardware result |
| --- | --- | --- |
| `4866c181…` | Rebuilt with fixed host owners | Boot logo only during protected window |
| `1959f0c2…` | Original seed, otherwise unchanged | Populated Settings |
| `211b084b…` | Rebuilt with checked anonymous owners | Same populated Settings, explicit sync/reset observed |
