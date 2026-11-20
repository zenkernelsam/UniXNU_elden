# Guarded camera DMA arena and native capture integration

The native camera board now accepts real owned frame-buffer storage through
`NativeDma.h` and `DmaBinding.h`. The allocator creates retained high-address
XNU RAM, verifies its original IOKit/PAR/cache mappings and builds the original
UniXNU4K table graph for a low32-bit camera IOVA. Both targets share one exact
CAMSS SID1c00 domain. Queue, completion, CPU borrow, return and retirement have
separate states. The code fully links over [CAMERA-CLOCKS.md](CAMERA-CLOCKS.md).

No tablet was accessed, no camera pixels were generated, and no physical SMMU/
rail/clock provider is installed automatically. A valid table or reference-clone
DT entry does not prove live hardware DMA, frame visibility or OS17 camera service.

## Source and actual native methods

Armada's frozen TB322FC/SM8750 `6f97250e` DTS uses `apps_smmu 0x1c00 0x00` for
CAMSS. Its original `camss.c` requires streaming/coherent DMA mask0xffffffff.
Rear S5KJNS RAW10 stride5104×3072, front GC08A8 RAW10 stride4080×2448 and exact
mode/CCI/PHY paths remain the accepted source-derived camera pipeline.

`dma-source.json` pins the already qualified `NativeMemory.cpp` and
`NativeMemoryMethods.inc`. `DmaSource.py` reuses six actual helper bodies:
output-width decoding, every-page IOKit physical-segment plus read/write PAR,
preserving PAR/DAIF during translation, cache-line discovery, WB clean and
barrier. Only types/symbol prefixes change; direct original OS17
getBytesNoCopy/prepare/getPhysicalSegment/IOMemoryMap exports remain used.
The captured wrong getBytesNoCopy slot43 is never used.

The MIT-licensed original ArmSmmu4K builder/context/audit/volatile table-reader
is the existing `83969cd874503e65997ff7639f7dcf3d0a92cd17` source import in
`drivers/EldenSmmu/upstream`. Preparation hashes its manifest and every source.
Production links those algorithms once through the existing sensor-memory
archive; the camera does not add a second copy or modify a shared SMMU controller.

## Frames and page graph

`EldenCameraDmaConfig` requires an original-owner chosen aligned low32-bit IOVA
and2..4 buffers per enabled sensor, up to8 total and128MiB allocation. One4KiB
guard precedes the first buffer, and another separates every pair/end. Physical
allocation contains the guards, but the device page tables map only each
page-rounded RAW10 buffer; guard IOVAs are absent. The exact byte extent returned
to the camera excludes page padding. Native allocation never chooses a fixed PA.

One original physically contiguous data descriptor and separate bounded table
descriptor are prepared and retained. Every4KiB primaryWB page must match its
original IOKit segment and actual read/write PAR0xff/PA. Segment boundaries are
allowed only if each page's PA continues exactly; gaps, overlaps, unsupported
widths, unprepared/partial aliases and mapping faults refuse.

After zeroing and WB clean/invalidation, a fresh Unique+Prefault NC alias must
independently verify every4KiB PAR0x44/PA. The WB data alias is not used again.
The table pool remains cached and is cleaned for the walker. Tables map exact
buffer ranges as RW/NC/inner-shareable/non-executable with explicit32-bit input
and real hardware output width. Capacity accounts for actual2MiB/1GiB IOVA
boundary crossings, not a fixed address or guessed table count. The physical
graph audit checks every used/unused entry before context encoding/install.

## Real original-owner gates

The original provider must read actual same-epoch SMMU IDs and reserve an unused
bank/ASID/slot, lowIOVA and exact SID1c00 for both sensors. Fixed known Gen4 IDs
are compared, while existing banks0..6/slots0..7 cannot be reused. Those comparisons
are not ownership evidence: the supplied provider must prove current shared
resource/IRQ/route facts. It must also preserve Wi-Fi/input/storage/DSP routes.

`early()` is required before allocation/reservation and again before installation.
It proves the actual early route owner has not sealed the touch snapshot. The
current one-WiFi BootRoute registry does not accept a camera route by itself;
mainline must install an exact reviewed multi-consumer early owner. No late-route
exception, pass-through shortcut, fabricated ATOS or private fixed bank is added.

Reservation and install attempts are recorded before callbacks; errors are not
proof that callbacks did nothing. The view carries actual tables/PA/context,
buffer identities and full owner/epoch/route epoch for independent physical
observation. Normal access checks current admission plus actual context/route/
fault visibility; frame completion additionally requires the physical provider
to prove device-written visibility. An integer pointer or SOF metadata alone
does not establish that. Failure closes normal admission and retains resources.

## Buffer lifecycle and actual camera wrappers

`DmaBinding.c` replaces the board's external buffer callback with the actual
allocator/route validator. All existing CCI/CSIPHY/CSID/VFE/sensor/clock/supply
functions execute. Physical receiver resources still supply real CSIPHY rails,
NoC/controller dependencies and actual DMA/IRQ drain. Their release must not
release dma's shared SMMU reservation a second time.

The queue operation records device ownership before its visibility callback;
failure retains the uncertain buffer. Duplicate queue, wrong target/owner/epoch/
IOVA/extent/mapping and a CPU-borrowed buffer are rejected. Real CAMSS completion
changes the slot to completed only after the actual pipeline's SOF/done and
physical visibility callback. The native dequeue wrapper then records a CPU
borrow with exact sequence/timestamp/error/extent. Explicit return must match
that record before requeue can happen. No raw device writes or synthetic pixels
are exposed by these interfaces.

During fault cleanup, only actual Board's serialized stop scope keeps DMA
cleanup admission while the original CCI sensor STOP/receiver drain/rail/clock
restore runs. Normal startup is not revived. Both cameras must finish Board
shutdown before the DMA owner proves physical device/IRQ/TLB/context retirement.
Its early SMR/S2CR route and every descriptor/table/alias remain reserved to
full reset, preserving the touch seal. Failure holds all references and callbacks.
Successful retirement clears device/completed slots, but an application-held
frame remains borrowed until actual explicit return. No speculative complete,
free, auto-reopen or identity reuse is implemented.

## Offline validation

Mac accepted folder: `/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes/
lab/camera-dma-20261120`.

- `host8`:51 scenarios/1408708 checks execute the actual native allocator and
  unchanged4K table graph. They cover both sensor buffer counts,2MiB/1GiB
  crossings, guards, stale lazy aliases, read/write PAR faults, PA holes,
  allocation/prepare/install/observation failures, early seal before/after
  reservation, original route preservation, attempted ownership, duplicate
  queues, exact borrow/return and failed/successful retirement. IOKit/PAR/cache/
  MMIO/physical route/DMA callbacks are explicitly modeled.
- `stack5`:actual DMA→Board→native clock/WL→sensor/CCI/CAMSS executes with real
  allocated mappings instead of synthetic buffer handles. Normal30206,
  refused-drain30372 and completion-visibility refusal30023 checks pass. Queue/
  borrow/explicit return/requeue and one failed retirement/retry execute.
  No actual camera pixels or hardware frame/visibility evidence is produced.
- `native3`:two scalar ARM64/iOS17 objects compile without warnings or automatic
  initializers. `kernel2` fully links, preserves every existing archive/recovery
  member and30 original Apple executable regions, and passes all prior pen,
  Actuator/RTC/wake/motion/SSC/pack/RPC/ADSP/QRTR/camera audits.
- 29 source/provenance checks,24 new actual-function startup negatives,32 prior
  peripheral guards and17 previous camera-clock guards pass. Direct original
  IOKit methods/PAR/DAIF/cache/barrier and unique4K/native providers are checked.
  Global ABI/loadable/runtime/relocation flags remain false.

Full kernel SHA256:
`a37eaa4b375ab091031009ad9a766620cbbae923be9c64ac9e2091cd92c2eba6`.
Native SHA256:
`322edf032717d007b4d855bd7dff5d26758723a6ee11ee2ff45708f741ba9f2c`.

Use `prepare_camera_dma.py`, `test_camera_dma.py`, `test_camera_dma_stack.py`,
`build_camera_dma.py`, `link_camera_dma.py`, `audit_camera_dma_link.py`,
`audit_camera_dma_source.py` and `test_camera_dma_audit.py`. Required paths are
in each tool's `--help`. Stack inputs remain accepted camera-clock source17,
shared-power source3, pipeline source9, CCI source7 and sensor-core source2.
Parent is source commit`09087b8`/camera-clock kernel3f59efa0. `dma-validation.json`
records complete inputs/result hashes and private archive. Workbench compact
receipts are under `lab/records/camera-dma-20261120`.

Next install the actual shared early SMMU/parent/RPMh/CSIPHY provider and collect
real RAW10 frames/retirement evidence, then calibration/ISP and OS17 camera
service. Mainline owns hardware testing. Camera DMA's offline acceptance is not
camera or whole-five-part Goal completion; Pencil/system PM/motion calibration/
UI, calibrated iOS motor effects and RTC persistence/wake remain required.
