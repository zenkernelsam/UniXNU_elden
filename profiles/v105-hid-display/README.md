# v105 display cache, HID delivery and real battery

Combines the compiled v104 display parent with the exact two v100 HID delivery
objects. The merger verifies all unchanged archive members and the original
core file list. The boot menu uses the already-tested compact battery flags
and extends the existing SMP hold from240 to600 seconds; the host requests
sync return at510 capture seconds. It fits1020 bytes, retaining fallback and
the8-second default. Kernel memory remains6GiB and RAM Data512MiB.

Candidate `b0fbb03b0105776075fc4023d73b2d2b1fa2fe2e969bd31d961b5344a99a52b6`,
session `5c80cd07c33e65a6`. Full entry/AVB/assets checks passed with floor
1738713600/footer0/parent3. User reports much smoother physical sliding;
32 full-copy samples have median58.80ms. This is not an end-to-end FPS figure.

Clicking Settings caused three backboardd faults at the same DC ZVA instruction,
then SpringBoard reloads. Later RAM Data ENOSPC is recorded separately. This
candidate is retained as the failing comparison for v106, not as a stable daily
image. No synthetic gestures were sent. Logs report83 dropped bytes. Typed
sync return and full-SHA stock recovery passed. See hardware-result.json.

Reproduce with `link_hid_delivery.py --profile v105-hid-display`, using the v104
build as `--v94-build` and the qualified v100 `--delivery-build`. Run
`audit_hid_kernel.py` and `audit_entry_relocation.py --profile v105-hid-display`,
then the normal entry/boot tools with `--hid-report`. All external inputs remain
the v91/v94 source-rebuilt assets and exact original21H461 kernelcache.
