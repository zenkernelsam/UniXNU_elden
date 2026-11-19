# Shared QRTR clients for the motion service

The original Elden `ShengQrtr` IPCRTR owner now serves kernel client ports.
`QrtrTransport.c` supplies the existing `NativeSsc` worker's actual transport
operations. The complete offline image includes the prior memory/cache/FastRPC
and ADSP close work. It does not start another DSP, router or sensor worker.
Genuine receive-intent ownership, remote-retirement proof, capture/calibration,
SMMU and physical samples remain required from mainline.

## Qualified source and original owner

`QrtrSource.py` hashes the current Elden `ShengQrtr.c`, `QcomQrtr.c` and header,
then derives changes without modifying the frozen base files. The shared QRTR
protocol/watch/frame builders come from Pagani same-SM8750/same-OS17
`c888efd3ce1b817d13f28783a77cf689cb2255c9`, with Sheng `08cde20e` provenance.
The exact imported bytes and sizes are in `qrtr-source.json`. Raw CRLF donor
files remain byte-bound in Git; generated sources normalize line endings before
applying documented anchors. Imported `AdspQrtr.c` records the client-selection,
copy/snapshot/callback and pure-frame method being adapted, not board settings.

The original Elden locator, protection-domain table, firmware/power/IRQ paths
and counters remain in the same owner. Donor IPCRTR intent sizes, PD mapping,
heap/bank and hardware registers are not substituted. Mainline must still
qualify the actual same-device sensor PD. Existing Wi-Fi QRTR/MHI is separate
and remains mainline-owned.

Three actual layout consumers rebuild together in `libEldenAdspRuntime.a`:
`AdspProbe.o`, `ShengQrtr.o` and `QcomQrtr.o`. All other archive members and
inherited component inputs are verified unchanged. The already qualified
`QcomGlink.o` from [ADSP-CHANNELS.md](ADSP-CHANNELS.md) remains untouched.
The original DSP executor calls the QRTR debt service after its existing send
pump; no polling thread, new periodic timer or second IPCRTR channel is added.

## Clients, identity and flow control

Twelve retained nonreusable client handles use independent ports. Three are
exposed by the actual SSC adapter: discovery, accelerometer and gyroscope.
Clients are unannounced local endpoints, as in the qualified donor. All three
observe one actual service400/instance/node/port and shared server generation;
unrelated list reorder does not change that generation. DEL_SERVER must name
the exact current service and instance, so an old deletion cannot remove its
replacement at the same address. Changed HELLO peer withdraws old identities.

Receive/send carry the real owner, owner epoch, channel epoch and server
generation. Wrong sender/local destination, stale service and malformed control
frames do not reach the motion client. A previously used handle cannot send
to a replacement server generation. Bounded receives are copied by the existing
motion worker; callbacks hold their references through actual return.

The donor corrects RESUME_TX: name the receiving local node/port and send it
from that port to the original sender. This matches Armada TB322FC/SM8750
`6f97250e` Linux `qrtr_send_resume_tx`/`qrtr_tx_resume`. The original ADSP queue
can refuse a reply, so this implementation retains bounded flow debt and retries
it on the same original executor. Duplicate debt coalesces; BYE/reset clears old
debt, and capacity overflow closes sensor admission rather than faking progress.

`QrtrPorts.h` requires a sleepable client-registry mutex distinct from the
original ADSP queue mutex. Lock order is client registry→bounded ADSP enqueue;
the existing ADSP drops its lock before callbacks. The final enqueue stays under
the registry lock so a same-link service replacement cannot race an old send
into the new address. Callbacks run without that lock. Providers must preserve
this order and thread-only execution; IRQ/spin-lock providers are invalid.

## Genuine receive configuration and stop

Install `EldenQrtrPortOps` before the original router starts. Its receive
intent size/count must come from reviewed original-owner evidence. The sensor
path requires capacity for a1024B payload plus the32B QRTR header. An unconfigured
startup preserves original1024×8 legacy intents and seals out later configuration;
it cannot advertise a usable sensor transport or silently resize live buffers.
The modeled test uses2048B only as a labeled host peer, not a production default.

Explicit `elden_motion_qrtr_transport` fills the real `EldenMotionTransport`
operations after owner setup; the original worker calls its open/level/send/
withdraw methods. There is no automatic call to this factory, owner setup or
`elden_motion_native_ssc_start` in the linked image.

Withdraw closes that port's admission, waits for actual callbacks/enqueues,
queues same-generation DEL_CLIENT when appropriate, then observes the original
shared queue/pump/callback state. Only after those local fences can the real
authority's retirement callback prove remote client inactivity or a stopped old
DSP generation. DEL_CLIENT queued is not an acknowledgement. A reconnect never
permits sending the old close into the new DSP session. Callback contexts and
handles remain retained through reset; no speculative DMA/resource release occurs.

Mainline must connect actual retirement and existing early SMMU route ownership,
same-device capture/PD registry, mount/calibration/time and samples. Complete
J317 SPU/CoreMotion/UI delivery remains separate; no orientation0x20/8a service
is published by this code.

## Accepted offline checks

Mac outputs `lab/motion-qrtr-20261120/source5`, `native5`, `kernel2` and `stack1`:

| Check | Executed code | Result |
| --- | --- | --- |
| Client/provider host | Actual original ShengQrtr/QcomQrtr, ports and SSC adapter | 16 scenarios,528 checks, ASan/UBSan |
| Combined stack | Actual ADSP registry/queue + GLINK + original QRTR + SSC transport | 18 scenarios,1954 checks, ASan/UBSan |
| Archive/call gates | Real compiled ar members and actual function boundaries | 18 positive/negative checks |
| Native | Five scalar ARM64 objects | Zero compile warnings, no SIMD/automatic initialization |
| Full link | Original Apple/peripheral/ADSP/RTC/recovery audits | Passed |

SMEM, locks, doorbells, DSP responses and physical retirement are explicit modeled
peers. Existing link-tool deprecation/segment-order warnings remain recorded.
Kernel SHA256:
`73aa1a9d874475a6f2c994dd8df80575bfd92f50086a92e3c8cf5bb1c1d34c2e`.
Native SHA256:
`db828bb8ea0ee370aca584d0244a2c89918fcdc86179fb10ef107abc72d820bc`.
Apple30, pen/motion207/10, Actuator168/186, RTC, camera, touch-sleep, SSC,
memory/cache/RPC, ADSP close and motion→haptics→RTC→UFS recovery audits pass.
The inherited ADSP IOBuffer targets remain prepare27/getBytes41/append43.
Global ABI/loadable/runtime/relocation flags remain false; no boot package or
tablet access occurred. The real providers and full five-part Goal remain open.

The archive audit composes only exact prior/current member fingerprints when
checking older component parents. Three-member changes require the explicit QRTR
profile, with original single-member gates retained for previous ADSP images.
Counterfeit/missing profile, unrelated changes and invalid cumulative proofs are
refused. RTC retention mode proves both real parent/current register-only
constructors, the exact callbacks and qualified RTC objects/shared SPMI members;
it does not equate initializer counts with preservation.

```sh
python3 tools/prepare_motion_qrtr.py --channel-source /qualified/adsp/source6 \
  --output /new/source
python3 tools/test_motion_qrtr.py --source /new/source \
  --base-native /qualified/v152/native --llvm /path/to/llvm/bin --output /new/native
python3 tools/test_motion_qrtr_stack.py --source /new/source --output /new/stack
python3 tools/link_motion_qrtr.py --parent /qualified/adsp/kernel2 \
  --qrtr-build /new/native --prelink-tool /qualified/prelink-tool \
  --original-kernelcache /original/21H461 --output /new/kernel
python3 tools/audit_motion_qrtr_link.py --kernel-build /new/kernel \
  --parent /qualified/adsp/kernel2 --prelink-tool /qualified/prelink-tool \
  --output /new/kernel/qrtr-audit.json
```

Evidence is `drivers/EldenMotion/qrtr-validation.json` and workbench
`lab/records/motion-qrtr-20261120`; private archive in `edl_work/motion-qrtr-20261120`.
Source stays on `offline-peripheral-fixes`, raw evidence outside research.
Port these changes against current mainline with its newer Wi-Fi/SMMU/input/
power/recovery work preserved; do not install this old frozen offline kernel
over the live candidate. Camera power/frames, real wake PM, calibrated system
haptics, RTC persistence and physical motion/UI still require completion.
