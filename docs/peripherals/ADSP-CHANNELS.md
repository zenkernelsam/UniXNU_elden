# Shared ADSP channels and the sensor FastRPC endpoint

The existing Elden ADSP registry and GLINK worker now expose epoch-aware
callbacks, observed channel status and an explicit close protocol. A native
adapter connects `fastrpcglink-apps-dsp` to the retained sensor memory/cache/RPC
chain from [SENSOR-MEMORY.md](SENSOR-MEMORY.md). These paths fully link offline.
Mainline still must supply genuine shared ownership, remote retirement, SMMU
and same-device sensor inputs before starting the sensor service.

## Source identity and compatibility

`AdspChannelSource.py` derives the changes from four hash-bound Elden inputs:
`drivers/EldenPmicGlink/AdspProbe.cpp`, `ShengAdsp.h`, `QcomGlink.h` and the frozen
`profiles/v91/native-build/extra/3d7e18c2cae2/QcomGlink.c`. Exact SHA256 values
are recorded in the driver and `adsp-validation.json`. The original firmware,
PMIC, power, interrupt and ADSP startup paths remain unchanged.

The qualified GLINK protocol donor is the same-SM8750 Pagani repository at
`c888efd3ce1b817d13f28783a77cf689cb2255c9`; its comments trace the improvements
to Sheng `08cde20e`. `glink-source.json` binds both imported files byte for byte.
Only the shared protocol is reused: late registration of received OPEN names,
owed RX_DONE under FIFO backpressure and atomic legacy CLOSE replies. Its board
registers, memory defaults and firmware/power settings are not imported.

Legacy `ShengAdspClient` and public entrypoints remain compatible. New clients
share the one original registry, six implemented GLINK channels and original
worker. Duplicate channel names are refused. The GLINK structure layout changes,
so all actual layout consumers must be rebuilt together: `AdspProbe.o` in
`libEldenAdspRuntime.a` and `QcomGlink.o` in `libEldenPmicTransport.a`. The final
archive audit verifies every other member against the inherited fingerprints.
No second DSP controller, GLINK worker or IPCRTR instance is introduced.

The inherited ADSP metadata code already uses the correct OS17 IOBuffer virtual
slots: prepare27 and getBytesNoCopy41. Slot43 is appendBytes. The final image
checks these exact original targets; the earlier separate Actuator defect is
not a reason to change the ADSP calls.

## Close and lifetime contract

`AdspChannel.h` exposes epoch callbacks plus independent observations of queued
work, pump/callback execution, closing, CLOSE sent, local CLOSE_ACK and peer
CLOSE. A close return of zero only latches the request. The original worker
discards that channel's queued data, finishes any partial OPEN handshake without
publishing the channel up, and sends the original protocol CLOSE.

Peer CLOSE does not imply receipt of our CLOSE_ACK. Handshake history survives
the original channel reset; status stays unfenced while either handshake or an
in-progress send/receive callback remains. Closed registrations are retained,
nonreusable and never automatically reopen. Fatal/corrupt GLINK closes callback
and send admission, but does not prove physical DSP or DMA inactivity.

`AdspEndpoint.h` uses exact channel epochs and source-qualified opaque FastRPC
cookies, permits one outstanding invocation, and suppresses duplicate/stale
responses. The shared authority must reserve nonreused cookies across all RPC
users; no fabricated token or per-endpoint monotonic assumption substitutes for
that authority. Attach and state notification are serialized so a connection
that precedes attach is delivered without an older snapshot overwriting it.
Wait/wake use the actual original event primitives.

Endpoint withdrawal first requires the observed GLINK queue/callback/CLOSE
fence, then calls the original authority's real remote-retirement provider.
Only that provider can establish that the sensor PD and any pending invocation
cannot still access retained memory. Subsequent SMMU withdrawal remains owned
by the memory route provider. Failure retains every context, mapping, callback
and table through platform reset. A GLINK handshake never permits freeing DMA
memory by itself.

## Mainline integration

After reviewing these changes against current mainline, the existing ADSP owner
provides `EldenSensorAdspAuthority`: real owner/epoch admission, global cookie
reserve/completion and actual remote-retirement proof. Intent size/count must
come from reviewed source/board evidence within the existing bounds.

The original sensor executor explicitly opens this endpoint, passes the returned
`EldenSensorEndpoint` to `elden_sensor_memory_binding_open`, then attaches that
binding before its first step. It also supplies the existing qualified SMMU
route and immutable same-device capture pack. Startup has no automatic caller
in this image. Keep current early-route/touch-seal/Wi-Fi coexistence and recovery
contracts; do not replace current mainline with this older frozen offline kernel.

The shared QRTR client interface for SSC, actual Gen4 sensor-PD registry,
calibration/mount/time/sample qualification and complete J317 SPU/CoreMotion/UI
contract remain missing. This endpoint is a concrete transport implementation,
not live sensor or rotation acceptance. Mainline alone owns hardware tests.

## Accepted offline evidence

Mac workspace `lab/adsp-channels-20261120` contains these accepted outputs:

| Output | What executed | Result |
| --- | --- | --- |
| source6/native10 | Actual original registry and GLINK methods | 17 scenarios,1554 checks |
| endpoint-source3/endpoint-native4 | Actual native endpoint methods | 9 scenarios,143 checks |
| endpoint-stack-source6 | Actual final source6 registry/GLINK plus endpoint | 18 scenarios,1823 checks |
| audit-tests1 | Actual compiled ar members/function boundaries; injected invalid declarations/calls | 14 positive/negative checks |
| kernel2 | Complete inherited component link and original Apple/peripheral/recovery audits | Passed |

Host tests use ASan/UBSan. SMEM, locks, scheduling, doorbells, OS services and
physical memory/remote ownership are explicit modeled peers. The endpoint
headers are unchanged between the recorded source5 parent and final source6;
the combined test was rerun against source6 after its actual close-method fix.
Three ARM64 objects contain no SIMD/FP or automatic initialization. Channel
objects compile without warnings; the endpoint retains one signedness warning
for its bounded connected latch passed to an `int` parameter.

Full kernel SHA256:
`e05271d2d252e61d147cebe5708590742886e8b8ad7d3ae753b0186219d4fc7e`.
Native SHA256:
`b3220203dec510c590a31dfbe68bafe2daa44629890226e807ac89d5aae6331f`.
Actual call edges, exact two-member replacement, original IOBuffer targets,
Apple30, pen/motion207/10, Actuator168/186, RTC, camera, input-sleep, SSC,
memory/cache/RPC and motion→haptics→RTC→UFS recovery pass final audit. All
global ABI/loadable/runtime/relocation flags remain false. No boot package,
tablet access or physical retirement/sample proof occurred.

```sh
python3 tools/prepare_adsp_channels.py --output /new/channel-source
python3 tools/test_adsp_channels.py --source /new/channel-source \
  --base-native /qualified/v152/native --llvm /path/to/llvm/bin \
  --output /new/channel-native
python3 tools/prepare_adsp_endpoint.py --channel-source /new/channel-source \
  --memory-source /qualified/sensor-memory/source4 --output /new/endpoint-source
python3 tools/test_adsp_endpoint.py --source /new/endpoint-source \
  --channel-source /new/channel-source --base-native /qualified/v152/native \
  --llvm /path/to/llvm/bin --output /new/endpoint-native
python3 tools/link_adsp_endpoint.py --parent /qualified/sensor-memory/kernel1 \
  --channel-build /new/channel-native --endpoint-build /new/endpoint-native \
  --prelink-tool /qualified/prelink-tool --original-kernelcache /original/21H461 \
  --output /new/kernel
python3 tools/audit_adsp_endpoint_link.py --kernel-build /new/kernel \
  --parent /qualified/sensor-memory/kernel1 --prelink-tool /qualified/prelink-tool \
  --output /new/kernel/adsp-audit.json
```

`peripheral_archive_audit.py` permits only the actual declared transport-member
changes; `peripheral_startup_audit.py` confines allowed internal binding calls
to actual compiled functions and verifies outer startup has no caller. The
updated SSC/memory/cache/RPC audits use these proofs without loosening unrelated
archive, startup or recovery checks.

Evidence: `drivers/EldenMotion/adsp-validation.json` and workbench
`lab/records/adsp-channels-20261120`; private output archive stays under
`edl_work/adsp-channels-20261120`. Source remains on `offline-peripheral-fixes`,
research Git-only. Real wake PM, calibrated system haptics, RTC persistence,
motion/UI and camera power/pipeline/frames remain; the five-part Goal is active.
