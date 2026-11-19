# Native sensor registry pack and FastRPC binding

The real vnode/SHA256/immutable-cache implementation now feeds the actual
FastRPC service through `PackBinding.c`. A source-bound host tool prepares the
same SSCPACK1 format from a reviewed Android capture manifest. These paths fully
link over [SENSOR-RPC.md](SENSOR-RPC.md), with no default calibration package or
automatic DSP startup. Real Y700 configuration/calibration capture and the shared
GLINK/DMA/power owner still need to be installed and hardware qualified.

## Original source and pack construction

`PackSource.py` selects only the exact `pack(files)` function from the same
SM8750/21H461 Pagani `c888efd3ce1b817d13f28783a77cf689cb2255c9` serializer.
The original7433B source SHA256 is
`b42dbf530f45403ed199f11d0c0ec9e17d96ec9e7e332d38cd78cdcf76e21e2b`.
Its BSD license is retained; the donor's permissive archive processing,
optional registry, library defaults and phone-specific identity are excluded.

`build_sensor_pack.py` requires an exact TB322FC/SM8750 capture manifest with
device/build/ADSP/adsp_dtb digests, observed calibration, the real
ADSP_LIBRARY_PATH, source tar/file lengths and SHA256, and every selected
canonical device path/member/length/hash. File payloads are never extracted to
the filesystem. Duplicate members, links/special files, traversal, ambiguous
path spellings, wrong board, oversized sources, missing configuration/registry,
unexpected library roots, unused sources and byte/hash mismatch refuse.
The input package must contain nonempty `sns_reg_config` and actual persistent
sensor data. It does not use another device's calibration.

The tool generates `pack.bin`, `seal.json` and an input record. A real observed
capture additionally emits private `EldenSensorPackInputs.h`; its device and
calibration identities must stay outside Git. Host-fixture provenance requires
explicit `--host-fixture`, emits no production header and is rejected by the
normal candidate path. A manifest is source evidence, not hardware acceptance;
the mainline authority must validate it against the actual boot/device state.

The seal binds the full file SHA256, directory-table/name SHA256 and a separate
registry digest. Registry hashing is byte-defined: for each sorted canonical
file under `/mnt/vendor/persist/sensors/`, SHA256 receives the path plus NUL,
little-endian u64 file length, and the file SHA256. Native code recomputes this
from actual cached bytes, so a matching outer digest cannot hide a wrong registry
identity. Device/build/firmware identities are verified by the original owner.

## Native file and live service

`NativePackMethods.inc` executes in both the native and actual-method test.
It opens only `/private/var/ngpu/sensors/pack.bin`, rejects symlinks anywhere in
the path and requires a regular root:root0444 file with stable size/file identity.
It holds one vnode for the complete read; partial reads, metadata replacement,
close errors, authority loss and digest/pack-geometry failures refuse publication.
Data not ready returns pending only before opening; a failed read retains the
buffer and never repeatedly reloads corrupted input. Total size is at most64MiB;
reads and hashing use <=64KiB chunks. The pack's full and namespace SHA256,
canonical extents, expected configuration/registry/library path and allowed
sensor/DSP file scope are all checked before the cache becomes ready.

After qualification every RPC file read uses the immutable retained cache.
There is no new filesystem open per DSP request. Owner/epoch/authority/Data and
stop checks still apply; range or identity errors preserve the caller's output.
Stop closes admission before trying the actual cache lock; an in-flight reader
keeps stop pending until it returns. Cache and all file buffers stay to reset.

`PackBinding.c` creates the original RPC provider with real native-cache
`pack_size`/`pack_read` methods. Transport, retained DMA IOVA, mapping/visibility,
opaque cookies and actual withdrawal remain the sole GLINK/DMA owner's operations.
The binding routes callbacks and the original worker into the existing RPC core,
requests stop/wakes its wait and serializes finish. Failed bus withdrawal cannot
become success; repeated finish does not withdraw a closed port twice. Every
context/buffer is retained until reset. No second IPCRTR, automatic worker,
SMMU mutation or fake physical-memory provider is installed.

## Accepted offline evidence

Mac outputs: `lab/sensor-pack-20261119/source7`, linked objects `native8`,
fixture integration `native9`, builder `builder2`, full image `kernel2`.
18 actual cache/binding scenarios1389 checks plus the externally built host-only
pack give19 scenarios1449 checks under ASan/UBSan. VFS/owner/DMA/clock are modeled;
SHA256, original pack parser and actual FastRPC provider methods execute.
The host builder passes17 scenarios20 checks on Windows and Mac. Native9's two
object bytes exactly equal the native8 objects included in the full kernel;
archive-container hashes differ and the individual ar members were compared.
Neither archive embeds test fixtures.

Full kernel SHA256 is
`d93b63ac7c85bb867b0c9a5e1b91595283c9e422146be19935a5ebaf357bd187`,
native `445e81bdf47b2788661ec26e9248f4f244bed7d5fc762637e48b292563e93f83`.
Two new scalar objects compile without warnings/automatic initializers. Actual
vnode/read/SHA/cache→RPC calls pass instruction audit; Apple30, existing
motion/pen207/10, Actuator168/186/directIOBuffer, RTC, camera/input-sleep and
motion→haptics→RTC→UFS recovery checks pass. Existing archives/object inputs are
retained and global ABI/loadable/runtime/relocation flags stay false.

```sh
python3 tools/build_sensor_pack.py --dump /actual/android-capture \
  --capture /reviewed/capture.json --output /new/private-pack
python3 tools/prepare_sensor_pack_native.py \
  --rpc-source /qualified/sensor-rpc/source7 --output /new/source
python3 tools/test_sensor_pack_native.py --source /new/source \
  --base-native /qualified/v152/native --llvm /path/to/llvm/bin \
  --output /new/native
python3 tools/link_sensor_pack_native.py --parent /qualified/sensor-rpc/kernel4 \
  --pack-build /new/native --prelink-tool /qualified/prelink-tool \
  --original-kernelcache /original/21H461 --output /new/kernel
python3 tools/audit_sensor_pack_link.py --kernel-build /new/kernel \
  --parent /qualified/sensor-rpc/kernel4 --prelink-tool /qualified/prelink-tool \
  --output /new/kernel/pack-audit.json
```

`test_sensor_pack_builder.py` creates only explicitly marked host fixtures.
Its output can be passed to the actual-method test with
`--host-fixture /builder/positive-host-only`; it cannot become a candidate input.
All outputs require new folders. Failed attempts and their logs remain retained.
Evidence is `drivers/EldenMotion/pack-validation.json` and workbench
`lab/records/sensor-pack-20261119`; private objects/images remain in
`edl_work/sensor-pack-20261119`, and research remains Git clones only.

Next install actual same-device captures and the real shared GLINK/DMA memory
provider, then prove sensor-PD replies and SSC samples. Complete the historical
J317 SPU/CoreMotion orientation contract before publishing orientation0x20/8a.
Camera power/frames, system wake, calibrated system haptics and persistent RTC
also remain. This file/cache milestone does not complete the five-part Goal.
