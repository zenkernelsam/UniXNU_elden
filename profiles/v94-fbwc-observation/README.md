# One additional post-input observation

This inherits v93's mapping, console guard, translation checker, copy path and
recovery logic. Only `EldenDisplayGlue.cpp` is overridden: the post-input preview
budget changes4→5 and the overall budget10→12. The other four compiled units'
loadable sections were compared byte-for-byte with v93 and are unchanged.

The v93 run passed the mapping checks and reduced median full-screen copy time
to134.15ms from716.65ms (5.34×). Its four post-tap previews ended during the
Settings fade-in, before the next frame logged by the display. This profile
collects that missing observation; it does not declare the faint screenshot
equivalent or hide a display failure.

Build with `tools/build_fbwc_kernel.py --profile v94-fbwc-observation` and the
same explicit v91 inputs described in `../v93-fbwc/README.md`. The build report
is passed to both the boot packer and auditor. It uses the same bare `fbwc`
activation key and the same1023-byte command line.

Both preview and return operations remain bounded. The maintained UI watcher
accepts `--finish-after-tap-seconds 45` for this short observation, while keeping
the old180-second default. The kernel watchdog and PC return remain independent.

Hardware result: mapping/read/write checks passed;17 full-copy samples had a
134.14ms median, and the final sampled Settings image matched the v91 preview.
Session-bound sync/reset and stock restoration passed. See `hardware-result.json`.
Actual finger input, rotation, long-run stability and60fps are not established.
