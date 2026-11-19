# Owned sensor DMA memory and FastRPC integration

The actual native allocator now maps retained high-address XNU RAM into a
qualified32-bit DSP IOVA using the existing UniXNU4K table algorithms.
`MemoryBinding.c` connects it to the native sealed cache and original FastRPC
service. All of these paths fully link over [SENSOR-PACK.md](SENSOR-PACK.md).
Physical SMMU/GLINK owners and genuine Y700 capture inputs still must be provided
by mainline; this has no new automatic startup or hardware acceptance.

## Exact source and memory geometry

Armada's exact TB322FC/SM8750 `6f97250e` specifies ADSP32-bit DMA and a session
tag at bit32. Session3..8 selects exact `100N,104N,106N,108N` stream IDs;
session number is not a hardware context-bank index. The allocator records
those four exact stream IDs, but never guesses an available bank or ASID.
The original shared SMMU owner reserves IOVA, bank, ASID and route epoch using
fresh physical state; Wi-Fi/other existing routes are not taken over here.

The table builder/context/audit/volatile table reader are the existing
MIT-licensed UniXNU ArmSmmu4K `83969cd874503e65997ff7639f7dcf3d0a92cd17`
imports in `drivers/EldenSmmu/upstream`. The source-bound preparation verifies
all original manifest fingerprints and copies algorithms without changes.
No competing GLINK/IPCRTR or MMIO SMMU controller is added. Supplied hardware
IDs, bank and ASID are not by themselves proof that the caller read real hardware.

`NativeMemoryMethods.inc` is executed by the actual-method host test. It obtains
one physically contiguous data descriptor,2MiB call space plus heap up64MiB,
and a separate bounded table pool. Both are prepared and retained to reset.
Data/table primary aliases must be normalWB; every4KiB page is checked through
the actual original IOKit physical-segment method and ARM read/write PAR.
Adjacent descriptor/IOPL segment boundaries are accepted only with continuous
page-level PA proof. Holes, faults, cache/PA mismatch, overlap and width overflow
refuse before publishing.

After initialization and WB clean/invalidation, a fresh Unique+Prefault NC
alias must independently pass every page's PAR0x44/PA verification. The WB data
alias is never read/written again; table memory remains cached and cleaned for
the device walker. No fixed low physical address is mapped. The low IOVA can
be chosen by the actual owner while backing PA remains in the high RAM region.
Exact current IOBuffer getBytesNoCopy/prepare/getPhysicalSegment and IOMemoryMap
getVirtualAddress exports are called directly, avoiding the captured wrong slot43.

Pages map RW/normalNC/inner-shareable/non-executable. The bounded physical
graph audit checks all present/unused descriptors before context encoding and
actual owner install/observe. Table capacity is calculated from actual IOVA
alignment, including a crossing2MiB boundary; it is not a fixed table count.
The view exposes the real owned table mapping and PA so the shared owner can
independently observe hardware TTBR/context/route/fault state. No hardware
ATOS capability is assumed on Elden.

## Live integration and shutdown

`MemoryBinding.h` connects the real memory object to the existing sealed
cache/RPC provider. Prepare checks exact arena/IOVA/size/session/process.
Publish calls the actual context observation and memory barrier; completion
rechecks the memory/route lease and barrier before reading DSP-written data.
The RPC live/wait boundary also checks nonsleeping actual memory admission,
so transport connectivity cannot hide an already-revoked memory route.
Fresh hardware/MMIO observations run separately from that nonblocking latch.

Stop closes public admission, wakes the original executor and finishes the
actual cache/RPC route. It withdraws the original GLINK endpoint first, then
requires genuine SMMU/translation/fault-IRQ/DMA withdrawal proof. Failure at
either stage retains all descriptors, table/data aliases, callbacks and objects.
Successful stop also retains these references until platform reset. Repeated
finish cannot withdraw the endpoint or route twice. No fabricated DMA idle,
automatic context reuse or speculative free is provided.

The shared owner must enforce early/exclusive route registration and current
touch/Wi-Fi coexistence contracts; this allocator is not permission to install
a late route after touch has sealed its original mapping snapshot. Mainline
must review that integration before hardware use.

## Accepted offline evidence

Mac outputs `lab/sensor-memory-20261119/source4`, `native4`, `kernel1`:
18 actual allocator/table/lease scenarios75240 checks and19 actual memory→native
sealed cache→FastRPC scenarios9989 checks pass ASan/UBSan. IOKit/cache/PAR/MMIO/
GLINK/VFS/clock/identity are explicit modeled peers; original table algorithms,
native lifecycle, source pack reader and SHA256 execute. Seven scalar objects
compile without warnings or automatic initializers.

Full kernel SHA256 is
`10a5d84dbffae54ee1377b46591aba51793646211c2bf5f1cc3a6c5792addf76`,
native `a44297b98f05384b2b04bc31f6628b2cec52508f3e3f99e8d3db8e3ceb1bfbf9`.
Actual original IOKit/PAR/cache/4K tables→pack→RPC calls pass final audit;
Apple30, existing pen/motion207/10, Actuator168/186/directIOBuffer, RTC,
camera/input-sleep and motion→haptics→RTC→UFS recovery audits pass. All older
archive/core inputs are held. Global ABI/loadable/runtime/relocation flags
remain false, and no boot package or tablet test occurred.

```sh
python3 tools/prepare_sensor_memory.py --output /new/source
python3 tools/test_sensor_memory.py --source /new/source \
  --pack-source /qualified/sensor-pack/source7 \
  --base-native /qualified/v152/native --llvm /path/to/llvm/bin \
  --output /new/native
python3 tools/link_sensor_memory.py --parent /qualified/sensor-pack/kernel2 \
  --memory-build /new/native --prelink-tool /qualified/prelink-tool \
  --original-kernelcache /original/21H461 --output /new/kernel
python3 tools/audit_sensor_memory_link.py --kernel-build /new/kernel \
  --parent /qualified/sensor-pack/kernel2 --memory-build /new/native \
  --prelink-tool /qualified/prelink-tool --output /new/kernel/memory-audit.json
```

Evidence: `drivers/EldenMotion/memory-validation.json` and workbench
`lab/records/sensor-memory-20261119`; private archive in `edl_work/sensor-memory-20261119`.
Code stays on this branch and research remains Git-only. The prepared header
changes only its relative import path; original algorithms do not change.
Current-mainline port must preserve its newer SMMU/Wi-Fi/power/input/recovery work.

Next native shared GLINK/route provider installation with actual same-device
registry/capture, prove DSP PD replies/SSC samples/mount/time, then complete
historical J317 SPU/CoreMotion/UI rotation. Camera power/frames, real system wake,
calibrated haptics and RTC persistence remain. Full five-part Goal stays active.
