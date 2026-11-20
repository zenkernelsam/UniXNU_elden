# Early sensor memory, listener and SSC lifecycle

The actual native runtime now connects early owned sensor memory, the retained
pack/FastRPC listener and the existing SSC sampling worker. The complete image
retains [MOTION-QRTR.md](MOTION-QRTR.md) and every prior peripheral component.
Physical SMMU/ADSP ownership, real receive intents, same-device calibration,
mount/time and live samples remain mainline responsibilities. This is an offline
implementation and link qualification, not a running sensor/rotation claim.

## Why startup is split

The original memory route must be installed before the current touch route seal.
Opening it only after Data/listener startup would conflict with that ordering.
`elden_sensor_runtime_prepare` therefore calls the existing actual sensor
allocator once, using the original SMMU owner's reserve/install/observe operations.
It installs the original QRTR receive configuration and the SSC status observer
before those services start. It neither boots the DSP nor creates another router.

Later, the explicit original-owner call to `elden_sensor_runtime_start` creates
one listener executor. `MemoryBinding.open_prepared` attaches the retained memory
object through its real view/admission, without reserving another IOVA, bank,
ASID or mapping. This keeps early route ownership separate from the later pack
read and listener execution. The existing `MemoryBinding.open` API remains valid.

The listener executor uses the existing concrete ADSP endpoint and native sealed
file cache. It starts the original SSC worker only after actual listener init2
has succeeded and the native pack cache is verified. Board admission/attributes/
time callbacks remain the real supplied qualifier. Missing identities, callbacks,
pack or ownership refuse; no fixture header, calibration or donor defaults are
installed by this runtime. Prepared memory descriptors and callback contexts are
retained through platform reset, including unsuccessful setup.

`SensorRuntimeConfig` requires the same owner/epoch for memory, ADSP, QRTR and
pack authorities. Intent sizes/counts, memory IOVA/session/size/process and real
seal digests are explicit inputs. The exact Elden/Armada TB322FC/SM8750 stream and
FastRPC rules are inherited from SENSOR-MEMORY/SENSOR-RPC; session3..8 is not a
hardware context bank. Original QRTR setup still requires its distinct sleepable
registry mutex and registry→ADSP queue lock order. No Wi-Fi provider is taken over.

Neither prepare nor start has a boot/constructor caller in the final image.
Mainline must install its actual early physical provider and opt-in startup
after reviewing current SMMU/input/ADSP/Wi-Fi/recovery changes. The frozen offline
kernel is a reproducibility output, not a replacement for current mainline.

## Stop, observer and timeout

FastRPC next2 can legitimately wait for the DSP indefinitely. Merely checking SSC
status between next2 calls would miss a motion failure while that call is waiting.
The existing SSC worker now offers one retained owner-qualified status observer,
installed before start. It invokes the observer after releasing its status lock;
the runtime uses it to latch stop and wake the existing listener when appropriate.
No new polling thread or fake interrupt is introduced. Legacy SSC startup seals
out a later observer, preserving the original single-owner lifetime.

Stop first closes sample/HID admission and requests actual SSC shutdown. The
listener remains live while the DSP may need reverse file operations to disable
and retire those clients. The actual terminal motion notification then cancels
the pending listener wait and permits endpoint/remote/SMMU withdrawal. Closing
the listener before that cleanup is a tested refusal case, not the intended path.
Runtime faults or the original stop deadline can also cancel the wait, with no
remote-inactivity success implied.

The platform's previous motion recovery call now invokes
`elden_sensor_runtime_recovery_stop`; when no runtime was configured it uses the
original legacy motion recovery. The aggregate observation budget stays5500ms.
There is no second5500ms budget added for RPC cleanup. Actual physical provider
callbacks must remain bounded; an elapsed observation deadline or missing proof
returns failure and retains memory. No PMIC/floor/reboot-routing changes occur.

After actual sampling termination, the runtime calls original memory-binding
finish: observed GLINK queue/callback/CLOSE fence, real remote PD/DMA retirement,
then actual SMMU withdrawal. Queued requests/DEL_CLIENT or handshake state are
not remote-inactivity proofs. Concurrent prepare/start/stop states are serialized;
partial setup, thread failure, a waiting callback or refusal cannot free any
descriptor/table/context or reopen a retained registration.

Runtime status separates original failure, cleanup failure, listener readiness,
motion state and terminal completion. Whole-runtime cleanup is required for the
new recovery wrapper to report success. These statuses do not replace physical
stop/fastboot/restoration evidence.

## Offline evidence

Accepted Mac outputs are `lab/sensor-runtime-20261120/source4`, `native4`,
`components5`, `kernel3` and `audit-tests2`:

| Check | Executed code | Result |
| --- | --- | --- |
| Runtime lifecycle | Actual SensorRuntimeMethods, modeled OS/lower providers | 16 scenarios,410 checks, ASan/UBSan |
| Prepared memory/cache/RPC | Actual allocator/4K tables, vnode/SHA, sealed cache and RPC | 20 scenarios,18580 checks |
| SSC observer | Actual original SSC worker/manager/codec | 7 scenarios,795 checks |
| Composed runtime | Actual allocator/cache/SHA/RPC/endpoint/QRTR/SSC/runtime | Normal9252 and retirement-refusal9220 checks |
| Link guard cases | Real compiled archive members and actual function boundaries | 32 positive/negative checks |
| Full image | Actual lifecycle edges, inherited ABI and recovery | Passed |

All host checks use ASan/UBSan. IOKit/cache/PAR/VFS, DSP responses, transport,
physical retirement, OS scheduler/clock and HID consumers are explicitly modeled
peers; this is not a source of production calibration or fabricated samples.
Three changed/new ARM64 objects compile scalar with zero warnings and no automatic
initializers. The existing UFS recovery compile retains26 warnings, and original
linker deprecation/segment-order warnings remain in the logs.

The actual native SSC observer and MemoryBinding archive member are the only
existing component bodies replaced. One SensorRuntimeNative object is added.
Only the original UFS recovery member is rebuilt to replace its motion wrapper
call. All other original archive members, including ADSP/GLINK/QRTR, remain
independently verified. Source-bound startup audits allow only the exact runtime
worker's endpoint/prepared-memory/transport/SSC calls and runtime prepare's QRTR
setup; the actual outer prepare/start still have no incoming callers.
Unrelated starts, missing runtime profile and counterfeit member hashes are refused.

Apple30, pen/motion207/10, Actuator168/186, RTC, camera, input-sleep, SSC,
memory/cache/RPC, ADSP and QRTR audits pass. Recovery retains runtime→haptics→
RTC→UFS order. Global ABI/loadable/runtime/relocation flags remain false. Exact
kernel SHA256 is `28d31bc51fb5b33706f3fd6da933d19d42a862129f5411e2ec658cc46b0c4ec8`;
native is `c20a3d21986507d3fc48d131d1a451ea3bb1f560fa20b49630277727ee63b0a0`.
Object/report fingerprints are in `drivers/EldenMotion/runtime-validation.json`.
No boot package, tablet or physical sensor acceptance occurred.

```sh
python3 tools/prepare_sensor_runtime.py --qrtr-source /qualified/qrtr/source5 \
  --memory-source /qualified/memory/source4 --pack-source /qualified/pack/source7 \
  --endpoint-source /qualified/adsp/endpoint-source3 --ssc-source /qualified/ssc/source6 \
  --output /new/source
python3 tools/test_sensor_runtime.py --source /new/source --base-native /qualified/v152/native \
  --llvm /path/to/llvm/bin --output /new/native
python3 tools/test_sensor_runtime_components.py --source /new/source \
  --motion-core /qualified/motion-core/source2 --output /new/components
python3 tools/link_sensor_runtime.py --parent /qualified/qrtr/kernel2 \
  --runtime-build /new/native --recovery-source /qualified/ssc/kernel6 \
  --prelink-tool /qualified/prelink-tool --original-kernelcache /original/21H461 \
  --output /new/kernel
python3 tools/audit_sensor_runtime_link.py --kernel-build /new/kernel \
  --parent /qualified/qrtr/kernel2 --runtime-build /new/native \
  --prelink-tool /qualified/prelink-tool --output /new/kernel/runtime-audit.json
```

The workbench receipt is `lab/records/sensor-runtime-20261120`, with private
outputs under `edl_work/sensor-runtime-20261120`. Keep raw evidence outside
research. Next install actual early physical owners and genuine PD/calibration/
mount/time/sample evidence, then complete J317 SPU/CoreMotion/UI delivery.
Camera power/pipeline/frames, real wake PM, calibrated system haptics and RTC
persistence also remain; the full five-part Goal stays active.
