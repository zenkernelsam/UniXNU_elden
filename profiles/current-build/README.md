# Current Bluetooth source-build manifest

This manifest describes the30 units that differ from the source-rebuildable v91
base in the current v121 Bluetooth branch. Providers resolve through v91 source
manifests, fixed Git dependencies, repository files,32 source snapshots and four
externally regenerated payload source files. No original object is a provider.

`historical_object_sha256` and source-origins.json are provenance, never files
opened by the builder. Virtual compiler paths preserve source spellings; a
closed Clang VFS maps all actual inputs into the new output. New snapshot files
retain their source license notices; original Apple/Qualcomm/Novatek payloads
are not stored here. The snapshots include exact applied patches where the
source file otherwise existed only in a former build directory.

The exporter ran once against the verified v121 link graph. It is not required
for a user build and is not a hidden preparatory dependency. The maintained
entrypoints are build_clean.py, build_current_units.py and link_current_kernel.py.
The expected native contract fixes loaded sections, addresses, flags and symbols;
UUID/DWARF/link-edit metadata can vary. Original Apple ranges are separately hashed.

Validation: fresh upstream dependency fetches, source reconstruction, all1010 XNU
and608 native objects,30 current units,15 project archives including Bluetooth,
final native/prelink and boot packaging passed. Denying reads of former research,
third_party and historical build trees still allowed all30 units to compile.
The complete single-command Bluetooth build also passed through boot audit.
The new image has not been flashed. GUIv109 has a separate gui-build manifest.
