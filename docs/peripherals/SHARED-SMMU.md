# Shared early SMMU for camera and ADSP

The camera and sensor memory owners now use one original early SMMU
transaction and an actual native MMIO/GIC provider. Their retained IOKit
allocators prepare a context first; normal frame/DSP access starts only after
the whole transaction commits. The original touch route checker is replaced
in its actual linked archive to accept that immutable transaction.

This is an offline native/full-link delivery on `offline-peripheral-fixes`,
over camera-DMA commit `ba26ee40d0184d293240249f12c54d1ad4c3a979`. No tablet,
boot package, hardware acceptance or mainline merge occurred. The real
boot/power/security/memory authority remains a mandatory input and has not
been installed. The five-part peripheral goal stays active.

Follow-up over source8d39028 re-audits every real context/root/table after
route writes and before batch publication. Injected post-route TTBR/table
corruption is now explicitly refused/poisoned. Accepted source12/host12/
stack13/native12/kernel12:173actual core scenarios1400032checks and11native
compositions2395232checks; all11final audits pass. Kerneld054975d/nativeb70f270e
supersedes the earlier component candidate below. Hardware/Goal status is
unchanged; original camera-DMA parent/native/recovery inputs are retained.

Current kernel:
`d054975d87c1c1aca07db77f6f2bac2884db48d03af8b7ada533ca10cadc2359`.
Current native:
`b70f270e75562fe2bd3d4d7aa3404f26f1ebd43cfef458a9d08d836cce6aa30d`.
Final implementation commit: `d7571f2f161051c0d0c130556d3b2915d841cd67`.
Current receipts: workbench `lab/records/shared-smmu-postroute-20261120`;
artifact `edl_work/shared-smmu-postroute-20261120/`
`shared-smmu-postroute-offline.tar.gz`,26,931,850B/SHA256
`9c9dddf658dc680a2d65e0de891d07be8bcabc9b4d29e584d91020142cfe1e10`;
all587archive members reverified on Windows. Earlier folders below retain
the first delivery for comparison, rather than representing the current image.

## Source and immutable transaction

The source audit reads Armada's frozen TB322FC/SM8750 Linux
`6f97250ead155fc3b980856323e99a4623dae73d` directly from Git. Camera uses
SID1c00. ADSP compute sessions3–8 have the effective SID set100N/104N/106N/108N.
For example, DT1004/mask80 plus1044/mask20 expands to those four exact SIDs;
session7 uses1007/mask40 plus exact1067/1087. The audit enumerates every
original masked set and proves equality with the four unmasked routes.
The implementation never extends the DT's effective match set.

`BootRoute.c` retains legacy single-Wi-Fi SID1401 APIs. New batch APIs reserve,
publish and seal once for explicitly selected Wi-Fi1401, camera1c00 and one
complete ADSP session. At most6rows; distinct slots>=8 and group banks>=7;
all4ADSP rows share one bank. Unknown/masked SIDs, partial/mixed sessions,
append/replacement/late install refuse. A release/acquire reservation marker
publishes immutable owner/epoch/plan only after state reservation. Touch
cannot seal an in-progress/poisoned transaction. Owner-bound read-only peek
works after seal; early allocation/lease/install APIs refuse. Registry
membership is bookkeeping, independent of physical ownership.

`Shared.c` reads all132slots/83banks, preserves boot slots0–6/banks0–6 and
CB0's complete inherited bypass SCTLR, and selects free separate banks/ASIDs/
slots. Explicit low32-bit IOVA apertures must be disjoint. PA checks include
whole retained data backing, unmapped camera guards and entire table capacity.
Overflow, cross-/intra-group overlap and range outside backing refuse before
writes. The already-linked MIT UniXNU ArmSmmu4K83969cd8 algorithms are reused
once without changes. Tables are4K RW/NC/inner-shareable/non-executable,
audited through actual retained volatile table reads. ID0.ATOSNS is set on
TB322FC, so no hardware ATOS write/translation claim is made.

Before each actual context/route write, the wrapper checks real early owner,
physical master idle, bank/ASID/aperture/security authority, retained memory,
IRQ state and full evolving slot/bank image. Only successful write callbacks
update expected state. Applied-error uncertainty never authorizes guessed
rollback. Original context validation/readback and documented read-only
ASIDPNE clearing are retained. Partial failure poisons the transaction and
closes normal admission while retaining cleanup/resources toreset.
Every selected context/root/physical-table graph is also re-observed after
the actual SMR/S2CR writes, before the actual observed batch is published.
Prior observations alone cannot qualify a later changed table image.

## Real native provider and startup order

`SharedNative.h` adopts actual retained SMMU15000000/1MiB and GIC16000000/64KiB
mappings from the sole original owner, rather than mapping a second
controller. Real ARM64 LDR/STR/barriers/counter instructions are compiled.
Original `armcpu_spi_attach`/`mask` registers global97 once and context IRQs
from the exact stock83-entry map. Mapping/power/clock, master idle, IOKit
backing and bank/ASID/stream/aperture/security callbacks are mandatory. ACTLR
is read and submitted to that authority; no guessed reset value or permissive
default is supplied. Retain all maps/cookies/owners/memory toreset.

The original touch owner supplies only slot7's baseline/active/tombstone
expectation under the same serialized lease; the core still reads actual
registers. No blanket peer override exists. Mainline must review its current
Wi-Fi/SMMU/touch owner before adopting this code; the old globalIRQ provider
cannot run concurrently.

1. Original boot owner establishes mappings and actual authority before
   touch's first snapshot. Call `elden_smmu_native_open`, obtain its one shared
   core, explicitly select groups/session/disjoint apertures.
2. Call `elden_smmu_peripheral_open` with the exact shared configuration and
   obtain camera/sensor adapters. They verify real retained native backing
   and device-written visibility; they never add another controller.
3. Call `elden_camera_dma_prepare_batch` and
   `elden_sensor_memory_native_prepare`. Original PA/PAR/WB→UniquePrefaultNC/
   table methods allocate and install each context. They publish no normal
   frame/DSP readiness. Premature view/admit/publish refuses without poisoning
   prepared state. If Wi-Fi is selected, its original owner supplies its
   existing retained memory to the same shared installer too; no new Wi-Fi
   arena/runtime adapter is delivered.
4. After every selected context passes real quiescence/table/IRQ/fault checks,
   call `elden_smmu_shared_commit`; actual route reads publish the whole batch.
   Call `elden_smmu_peripheral_activate`, then each native-memory activate.
   Normal admission rechecks committed context/route/authority.
5. Touch may seal and run its original DMA/HID path. Camera passes activated
   memory to existing `DmaBinding`/Board. Sensors use
   `elden_sensor_runtime_adopt` with exact owner/epoch/IOVA/size/session/process,
   then original SSC/QRTR/FastRPC runtime start. It attaches the existing
   native memory, never allocates or installs another route.

These entry points have no automatic caller in the delivered image. Actual
current-mainline early UFS/touch/physical power startup integration remains
required. Do not add a late exception, no-op physical proof or another IRQ owner.

## Faults and retirement

A context/global fault or failed read closes normal admission. Handler masks
only its registered line, captures bounded metadata once and leaves SMMU
faults latched. No allocation, console locking, global acknowledgement or fake
completion. Camera still requires original pipeline SOF/done and real
device-written visibility; queued/completed/CPU-borrowed/explicitly-returned
states remain distinct. No synthetic pixel/sample/CPU return is produced.

After real Board/sensor/CSI/DMA or ADSP endpoint/remote/callback stop, the
physical owner proves master idle. Context retirement closes admission,
masks/drains its own GIC line and in-flight handler, then original programmer
verifies the exact owned context, disables it and performs ASID invalidation/
TLBSYNC. SMR/S2CR/CBAR/CBA2R and memory remain reserved toreset; never replay
unused S2CR0300006e or free/reuse a context. Global97 stays retained until all
attempted context images are actually disabled/TLB-synchronized and masters
idle. Masked IRQs alone are insufficient; one peer's successful stop cannot
hide another's TLB timeout. Uncertainty retains resources for reset.

Original sensor→haptics→RTC→UFS recovery wrapper/aggregate5500ms remains exact.
No independent watchdog/reset route is added. Mainline must connect its
actual cleanup calls to these explicit APIs before hardware use.

## Accepted offline evidence and reproduction

Mac: `/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes/`
`lab/shared-smmu-20261120`. Accepted source11/host11/stack12/native11/kernel11;
camera10/sensor10; runtime's original source4 with current memory header and
adopt methods. Workbench evidence: `lab/records/shared-smmu-20261120`; private
artifacts in `edl_work/shared-smmu-20261120`. Source/result/artifact hashes are
recorded in `drivers/EldenSmmu/shared-validation.json`.

- 171actual shared/batch scenarios1272415checks include full/mixed sessions,
  100six-row reserve/seal races, every original programmer write failure/
  applied-error, foreign slot/bank/ASID, late seal and PA/guard overlap.
- 11actual native/allocator/touch/GIC compositions2375292checks cover precommit
  refusal, staged commit/activate, global/context faults, physical idle refusal,
  TLB timeout and peer/global retirement ordering.
- Camera allocator53/1509203; sensor allocator20/91717 plus actual memory/
  cache/RPC20/18580; runtime prepare/adopt19scenarios. Schedule-dependent
  runtime check totals are recorded, not hardware measurements. Original full
  camera-DMA→Board→CCI/CAMSS normal30206/drain30372/visibility30023 still passes.
- Legacy Wi-Fi/touch122/11488 and domain25/4020100 pass ASan/UBSan. Actual
  production helpers execute; IOKit/PAR/cache/MMIO/GIC/ACTLR/security/firmware
  peers are explicitly modeled.
- Eight scalar ARM64/iOS17 objects0compilewarnings/noinit. Final link changes
  only `EldenNvtDmaRoute.o` and `NativeMemory.o` in original archives, plus
  direct `NativeCameraDma.o` and `SensorRuntimeNative.o`. Every other original
  archive/object/recovery input and30Apple executable regions are retained.
  Original4K is linked once; global ABI/loadable/runtime/relocation flags false.
- All11final audits pass: shared/camera/runtime/sleep/motion/haptics/RTC/QRTR,
  plus21shared/32prior/24camera actual archive/startup negative checks.

Kernel:
`2d078d215366d807162a7ac6c7c2d5c1e7094e9e4fde36df27360421b4f404dc`.
Native:
`a568b41862a4d06bad6398e65bb9232720517f9f9bbfefae91d02eceb28009e4`.

Tools: prepare/test/build/link/audit shared_smmu and
`test_shared_smmu_native.py`, `test_shared_smmu_audit.py`; required qualified
paths are in `--help`. Build requires original OS17 native header capture,
kernelcache and accepted camera-DMA parent, rebuilds new source objects and
retains named inputs. It is not a boot/clean-current-mainline claim. Earlier
failed attempts/logs remain retained.

Still needed: actual original shared startup/physical authority and DMA;
Gen4sensorPD/registry/capture/calibration/mount/time/samples/rotation UI;
camera PLL/RPMh/CSIrails/RAW10/calibration/ISP/OS17 camera service; Pencil
acceptance, screen/systemPM wake, measured haptics/system effects and RTC
persistence/alarm. This component does not close those objectives or the
whole five-part Goal.
