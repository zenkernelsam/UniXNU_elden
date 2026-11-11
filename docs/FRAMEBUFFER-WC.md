# Framebuffer copy improvement · 2026-11-12

The same 23,152,640-byte full-frame CPU copy now takes a median **134.14ms**,
compared with **716.65ms** in the source-rebuilt v91 control: **5.34× faster**.
Both measurements contain17 full-copy samples and use the19.2MHz physical
counter around the copy and its final DSB. This is not an end-to-end FPS result.

The old mapping used device/inhibited-cache attributes. The experiment prevents
the early text-console alias, requires the framebuffer outside the main physmap,
sets the descriptor and CPU mapping to Normal Non-Cacheable/write-combine, and
checks every mapped page for both read and write translation before pixel access.
The measured1904×3040/stride7616 geometry and PA0xfc800000 must match.5653 pages
passed, including the rounded final page; the existing watchdog must be armed.

The early console guard was checked in machine code: no vector operations occur
on its new return path. Existing XNU console vectors were not misreported as new
driver SIMD. The four native changed/added units remain scalar. Original Apple
executable extents and all unchanged source-built objects are separately checked.

The v93 test proved mapping and copy timing but exhausted its screenshot budget
during Settings fade-in. The v94 observation variant changed only two preview
limits; other mapping/copy/recovery unit sections were byte-identical. It then
captured the populated Settings page. The238×380 RGB565 preview and resulting
PNG match the prior baseline exactly (CRC3bd2f297); this is sampled-image parity,
not a claim of every1904×3040 BGRA byte or panel-vblank correctness. Explicit
session-bound sync/reset and full-hash restoration to Android passed. The v94
capture was4,960,298 bytes and reported40 dropped bytes; the selected frame and
reset markers were complete.

Current tested candidate:
`cea9703fdb9a98a8bf48ea9a885242fe7caafee5de20c62279682f0ff2b4bc80`,
session `bc0a28dcaf1688e2`. Boot rollback remains1738713600/footer0, parent3.
See `profiles/v94-fbwc-observation/hardware-result.json` and `static-audit.json`.

Reproduction uses `tools/build_fbwc_kernel.py` with the verified v91 core/native/
image outputs and `--profile v94-fbwc-observation`, then the existing entry, boot
and audit tools with `--fbwc-report` and the entry report. Detailed commands and
boundaries are in the two framebuffer profile READMEs.

Reference method: NativeMobileDisplay `9e611318` uses the same IOKit cache mode
for its T733 path; this is API guidance, not evidence that its board constants
apply to Y700. TB322FC Linux `bf96c3d4` supplies the independent framebuffer
base/geometry; ArmadaOS's runtime-populated simplefb template remains the board
reference. No charger, DVFS, GPU, speaker or device partition policy changed.

Next acceptance is a real finger swipe/tap/scroll and stable display observation.
The133–139ms copy range is still far from a60fps budget, and synthetic vsync and
software-renderer costs remain. The complete peripheral Goal stays open.
