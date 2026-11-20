# Gen4 camera receive pipeline and native capture binding

The exact TB322FC/SM8750 receive path now has source-derived CSIPHY2.4,
CSID980 and VFE Gen4 methods, a native MMIO/GIC adapter and a binding to the
existing front/rear sensor/CCI code. They fully link over the accepted
[SENSOR-RUNTIME.md](SENSOR-RUNTIME.md) image. Actual board power, clock/rails,
exclusive SMMU/IRQ ownership, retained frame buffers and physical DMA stop proof
remain required; no tablet, camera frame or OS17 camera app was tested.

## Source and exact path

Primary source is Armada's fixed TB322FC kernel
`6f97250ead155fc3b980856323e99a4623dae73d`. `pipeline-source.json` hashes every
original imported CAMSS and DTS file. `PipelineSource.py` extracts26 actual
functions and the original PHY lane table/register definitions. Only owned
read/write access, bounded timestamp/reset handling, callback metadata and the
qualified Gen4-only branch change. Original GPL and copyright notices remain
with the source. No SM8250/SM8650 VFE register layout is substituted.

The actual `sm8750_resources` chooses `csid_ops_980` and `vfe_ops_gen4`.
CSID/VFE indices must be paired. Full decoders0..2 have three usable RDI lines;
lite decoders3..4 have four. The original owner explicitly chooses an unused
decoder/RDI and maintains whole-controller ownership; a DTS entry is not a lease.

| Sensor path | PHY | PHY mapping / GIC INTID | CCI | Expected sensor mode |
| --- | --- | --- | --- | --- |
| Rear S5KJNS |3 |`0xadaf000`,0x2000 /480 |CCI0/master0 |4080×3072 GRBG RAW10,672MHz link |
| Front GC08A8 |4 |`0xadb1000`,0x2000 /154 |CCI1/master0 |3264×2448 RGGB RAW10,336MHz link |

Receiver lane positions0..3 are the actual CAMSS endpoints; sensor endpoint
lanes1..4 are not shifted into the receiver configuration. PHY uses the exact
`lane_regs_2_4_0` table, common register offset0x1000/status offset0x138 and
400MHz timer. Source settling arithmetic yields29/31 for rear/front. All
resource addresses, lengths,16 named IRQs,32-bit DMA mask, SID1c00 and two
initial buffers are verified by `audit_camera_pipeline_board.py` (36 checks).
These are source facts, not observed ownership or running hardware evidence.

The original VFE Gen4 stores image address and frame increment in256-byte units,
uses write master23+RDI for full VFE and RDI for lite, and receives SOF/frame-done
through CSID. Its VFE reset/halt functions are not physical drain proofs and are
not used as successful placeholders here. CSID's real reset acknowledgment has
the original500ms bound. High/low/high timestamp reads are bounded to32 retries.
No test-pattern generator or synthetic frame pixels are added.

## Buffers, queue and frame delivery

`Pipeline.h` requires explicit owner/epoch/route epoch, actual32-bit contiguous
IOVA, mapped CPU address and retained buffer extent. The provider must validate
the physical mapping/cache/route before queue and actual device-written visibility
before completion. A valid integer address or pointer alone does not prove that.
Addresses are256-byte aligned; extent and page route must match the same owner.
Overlap, reused active IDs, stale lease, unavailable buffers and output overflow
refuse. Eight buffer identities stay retained; only a returned frame can be queued
again with the identical mapping metadata.

The original two-buffer VFE queue/update/SOF/completion functions execute.
RAW10 stride is rounded to16B: rear5104B/front4080B. Frame metadata is published
only after actual CSID SOF/done processing and the original buffer visibility
provider. Consecutive completion sequence/time are checked. An absent SOF,
unknown status, malformed queue or revoked provider closes admission. Consumer
output remains unchanged on refusal. Source-specific packed RAW10 is retained;
debayer/ISP and OS17 CMIO conversion are not provided by this receive stage.

Before configuring global VFE clock-gating override, preflight checks every
actual full28/lite6 write client and every CSID RDI. Foreign active streams,
pending interrupt state, powered PHY or invalid versions refuse before writes.
Owned timing/masks are restored only after actual source/DMA drain and readback.
No foreign status is cleared to manufacture idle or progress.

## Native ownership and sensor integration

`NativePipeline.cpp` maps only the exact source-selected PHY/CSID/paired VFE and
GIC windows. It retains an owned registration for each exact IRQ and verifies
mask/RWP/active/pending/inflight state. Its exclusive decoder/PHY claims are never
reused before reset. Mapping or attach failure keeps its partial context; it
does not mask a peer's failed attach. Original camera worker calls `service`
to observe the actual peripheral status while GIC parents remain masked.
Unexpected primary IRQ closes admission and remains visible in native status.
This is the current bounded polled integration, not qualification of continuous
interrupt-driven capture or power-saving behavior.

All accesses additionally require the original physical power/clock/pin owner.
Native MMIO uses plain scalar LDR/STR and barriers, not compiler-generated paired
device-memory reads. No generic C++ allocator import is added: retained native
contexts use original kernel IOMalloc. No automatic boot entry, camera thread,
second CAMSS/SMMU owner or PD/rail vote is installed.

`CaptureBinding.c` joins the existing actual CCI sensor control to this receiver.
The original board first opens CCI and native receiver with the same identity,
then explicitly binds/probes. Queue at least two real owned buffers, start the
receiver and then the original source-derived sensor. The original reset/supply/
MCLK resource callbacks remain mandatory. Expected S5KJNS/GC08A8 ID and gain
limits do not authorize guessed rails or another board's PMIC settings.

Stop writes the original sensor stream-off, then calls the native receiver's
real source/CSI/VFE/DMA/IRQ/fault drain provider before releasing sensor rails.
WM-disable readback alone is not that provider. Core/native error or failed drain
retains sensor power, mappings and callback objects. Partial sensor startup still
allows explicit receiver cleanup; completed capture STOP is idempotent. Successful
cleanup retains mappings through actual platform reset, and the native decoder
claim cannot silently reopen. Camera system recovery/start ownership must be
integrated by mainline before hardware use.

## Accepted offline evidence

Mac outputs `lab/camera-pipeline-20261120/source8`, `native8`, `native9`,
`capture-stack4`, `kernel1`, `audit-tests1` and final `source9` provenance:

| Check | Actual code executed | Result |
| --- | --- | --- |
| Register/queue core | Exact PHY/CSID/VFE functions; every start/stop access failure, consecutive frames |218 scenarios,1202627 checks, ASan/UBSan |
| Native adapter | Actual mapping/IRQ adapter and core |8 cases,5077 checks |
| Composed capture | Actual front/rear sensor/Core/CCI/native receiver/capture binding |Normal50667 / drain-refusal50774 checks |
| Startup audit | Actual compiled function boundaries with injected invalid calls |10 checks |
| Board audit | Actual fixed DTS/resource/operator selection |36 checks |
| Full kernel | Original Apple/peripheral/recovery audits |Passed |

Power, MMIO, GIC, physical DMA/buffer visibility and sensor responses are explicit
host peers. Tests generate no production camera assets or frame contents. Three
ARM64 objects compile scalar without warnings/automatic initializers. Existing
link-tool deprecation and segment-order warnings remain in the logs.

Full kernel SHA256:
`70a217137db6d6404d6c05c8412e314ff41b4fa6952faae89f999ca8a26a67e4`.
Native SHA256:
`6641316f764eea9b1d77889d3c0fc26cb552bb5f3c1c78985a3b88c5e62650be`.
Final native9 objects match the linked native8 objects exactly. Final source9
adds complete topology provenance; its20 prepared source files match compiled
source8 bytes exactly, as `final-source-proof.json` records. All existing native
archives and recovery object are unchanged. Apple30, pen/motion207/10,
Actuator168/186, RTC, input-sleep, SSC, memory/cache/RPC, ADSP/QRTR and unified
sensor recovery audits pass; global ABI/loadable/runtime/relocation flags remain
false. No new boot package or tablet action occurred.

```sh
python3 tools/prepare_camera_pipeline.py --output /new/source
python3 tools/test_camera_pipeline.py --source /new/source --llvm /path/to/llvm/bin \
  --base-native /qualified/v152/native --output /new/native
python3 tools/test_camera_capture_stack.py --pipeline-source /new/source \
  --cci-source /qualified/camera-cci/source7 --sensor-source /qualified/camera-core/source2 \
  --output /new/capture-stack
python3 tools/link_camera_pipeline.py --parent /qualified/sensor-runtime/kernel3 \
  --pipeline-build /new/native --prelink-tool /qualified/prelink-tool \
  --original-kernelcache /original/21H461 --output /new/kernel
python3 tools/audit_camera_pipeline_link.py --kernel-build /new/kernel \
  --parent /qualified/sensor-runtime/kernel3 --pipeline-build /new/native \
  --prelink-tool /qualified/prelink-tool --output /new/kernel/pipeline-audit.json
```

Source remains on `offline-peripheral-fixes`; validation entry is
`drivers/EldenCamera/pipeline-validation.json`, workbench receipt
`lab/records/camera-pipeline-20261120`, private objects/outputs in corresponding
`edl_work`. Mainline must supply actual board rails/clocks/pins, early immutable
SMMU/DMA/IRQ/buffer ownership and hardware stop evidence, then capture real RAW10
frames and implement ISP/debayer/OS17 camera service. Preserve its current Wi-Fi,
input, memory, GPU and recovery work when porting. The five-part Goal stays active.
