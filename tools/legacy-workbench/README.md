# Archived workbench recipes

These are immutable historical source copies, preserved with the original
fingerprints in `import-manifest.json`. Its "path migration incomplete" status
describes the capture, not a promise that these old commands are supported.
Do not run old hardware/build recipes without adapting and reviewing them.

The maintained v91 build is documented in `../../docs/REPRODUCIBILITY.md` and
the `profiles/v91/*-build` profiles. Maintained device tools live in
`../device/`; reference synchronization lives in `../maintenance/`.
The new build pipeline does not need old boot carriers or Patch source files.
Some pure helper functions are imported here by the maintained tools; they
remain source-controlled and are exercised by the final candidate audit.

The 447 matching top-level workbench source copies were moved into its archive.
Seven compatibility launchers remain in `edl_work`; their implementations are
in this repository. Historical JSON records retain their original paths and
hashes. Consult the workbench maintenance relocation/recycle manifests when
reading a retired artifact reference.
