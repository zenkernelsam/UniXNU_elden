# v104 source mapping cache and bounded content scans

This compiled parent preserves v95's scalar paired-store copy and reuses the
exact BSD `NativeLegacySourceCache.h` from Pagani-Drivers c888efd3. Four retained
descriptor/map pairs expire after16 intervening swaps, and are released on
display-off, stop and free. Mapping attributes and synchronous swap completion
remain unchanged. The original cache test passes six scenarios under ASan/UBSan;
the existing469 copy rectangles and framebuffer translation-policy tests pass.

The expensive full-frame content/color scans now run only while the existing
preview budget needs classification. Every actual copy and retirement still
runs, and preview source IDs track transfers. This follows the upstream move
away from steady full-pixel diagnostic scans. The display remains a CPU path.

Build with `tools/build_fbwc_kernel.py --profile v104-display-cache` and the
v95 recipe's verified v91 core/native/image parents. Five units are rebuilt;
the original console vectors are preserved, while mapping/display/glue units
are scalar.30 original Apple execution ranges are unchanged. See build/test
records here. This parent was exercised in the combined v105 candidate, not
as an independent signed image; attribute combined behavior accordingly.
