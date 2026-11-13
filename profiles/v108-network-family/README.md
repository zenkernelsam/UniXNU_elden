# v108 original IONetworkingFamily

v107 showed that the original network-stack provider was absent. This profile
adds the exact 21H461 IONetworkingFamily to the original prelink roots. The
native kernel is byte-identical to v107; original personalities/dependencies
are retained, all previous30 Apple execution extents remain unchanged, and
the added91,284-byte execution extent matches the original SHA in plan.json.
No dummy network interface, MAC address or shortened timeout is supplied.

The signed candidate passed the real device: all six complete snapshots show
busy0/providers3 (IONetworkStack and IOAESAccelerator). No Wi-Fi/Ethernet
interface exists yet. This run did not exercise About; its latency is not
claimed fixed. The capture is1,475,520 bytes, drop36; typed sync/reset,
actual fastboot and full-SHA stock Android restoration all passed.

## Reproduction

First reproduce v107 and its inherited v106/v105/v104/v100 input chain. Use
the exact original IPSW kernelcache and XnuPrelink commit from the v91
image-build profile. Firmware, loader, Data seed and signing prerequisites
remain those in docs/REPRODUCIBILITY.md and the v106 profile.

```sh
python tools/build_network_family.py --baseline "$V107" \
  --original-kernelcache "$ORIGINAL" --prelink-repository "$PRELINK" --output "$OUT"
python tools/audit_network_family.py --build "$OUT" \
  --prelink-repository "$PRELINK" --output "$OUT/kernel-audit.json"
python tools/audit_entry_relocation.py --profile v108-network-family \
  --baseline "$V107" --build "$OUT" --core "$V91_CORE" --output "$OUT/entry-layout.json"
```

Use absolute paths. Create prepare_v91_entry.py's plan from the produced
kernel SHA plus v91's original_executable_sha256. Apply only the existing
104-byte entry transform and qualified AMFI word. Build and independently
audit the boot image with --hid-report "$OUT/result.json" and the normal
v106 menu/assets; floor1738713600/footer0/parent3 and6GiB remain.

Initial relative original-input path failed before prelink. build2 completed;
build3 fixes the saved native-link command's destination for later derivation,
without executing that command, and re-prelinks to the identical kernel SHA.
The byte-identical build3 output does not require a duplicate hardware test.
Both attempts remain under Mac lab/network-v108-20261114; final is build3.
Windows candidate and raw session are in workbench edl_work/network-v108-20261114
and edl_work/live-network-v108-20261114. Checked-in JSON records bind the results.
