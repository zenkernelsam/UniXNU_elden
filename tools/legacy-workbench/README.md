# Archived workbench recipes

These source files are the exact Windows workbench recipes imported on 2026-11-11.
`import-manifest.json` records their source hashes. They preserve the build,
signing admission, full-image verification, USB console, unattended reset,
partitioning and historical diagnostic implementation for review.

They are **not a portable build entrypoint**. Some recipes depend on a prior
version's generated artifacts, original OS/firmware inputs and host-specific
paths. In particular, historical `research/<experiment>` paths now resolve via
the workbench's `maintenance/cleanup-20261111/research-moves.json`. Do not run a
legacy recipe against a tablet to discover whether its assumptions still hold.

Source fixes belong in this repository. Workbench copies are frozen historical
inputs; never edit them independently. Port an active recipe to `tools/` with
explicit paths and tests before changing it. The maintained reference updater is
[`../maintenance/sync_reference_repos.py`](../maintenance/sync_reference_repos.py).

External binaries, keys, passwords and backups are not imported. Runtime device
identities in these recipes are historical selection guards, not defaults for
another owner's device. A public reproduction must supply its own authorized
device configuration and matching external inputs.
