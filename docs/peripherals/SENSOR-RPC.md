# Sensor PD reverse file service

The source-derived FastRPC listener, read-only registry file service and actual
owner/provider binding now compile and fully link over [MOTION-SSC.md](MOTION-SSC.md).
They retain the existing motion, input-sleep, haptics, RTC and camera code.
No GLINK owner, DMA arena or original Y700 calibration pack is installed;
therefore no sensor PD, sample stream or automatic rotation has been accepted.

## Exact source and transport

The fixed same-SM8750/21H461 Pagani source
`c888efd3ce1b817d13f28783a77cf689cb2255c9` provides the listener/file protocol.
Seven original files/tests are hash bound in `rpc-source.json`, with their
BSD-2-Clause-Patent license. Its phone-specific native mapping code is excluded.
`RpcSource.py` lists every uniquely anchored derivation; original file-operation
and metadata bodies remain present with prefixes and explicit lifetime checks.

The exact TB322FC Linux `6f97250e` FastRPC source is63705B, SHA256
`453952957b8a6b93d2c3a8685e391004361e3a730b2213042dec216b62157c4c`.
It confirms sensors PD2,40B invoke,12B reply prefix, context-ID shift4/mask0xff0,
ADSP32-bit DMA and session tag at bit32 for `qcom,fastrpc`. The tag comes from
the compute node's DT `reg`, not the actual SMMU hardware context-bank index.
The compatible `qcom,kaanapali-fastrpc` instead shifts56; that is not this
SM8750 board. Exact Elden stream lists3..8 remain recorded in MOTION-SSC.md.

## Implemented path

The original owner runs the service through `SensorRpc.h`; it provides a
retained contiguous DMA IOVA/alias, actual session3..8, owner/epoch, memory
visibility/route admission, and its existing `fastrpcglink-apps-dsp` transport.
No fixed0xd0100000 heap, bank4 default, pass-through mapping, late SMMU mutation
or new channel/thread is created. Call area is2MiB plus nonzero heap, up64MiB,
page aligned and within a real32-bit IOVA. Physical address and IOVA are distinct.

The service attaches, opens/registers `adsp_default_listener`, initializes the
listener and handles reverse `apps_std`/`apps_remotectl`/`apps_mem` calls.
File operations read only the verified pack: open/read/line/seek/size/EOF,
directory listings and stat. File creation/write/remove/truncate/rename,
environment mutation and unproved heap unmap refuse. Unknown interfaces fail.
Actual heap mmap must return a valid32-bit DSP address; no silent truncation.

The shared provider must reserve nonreused opaque request cookies with PD2
and retire them only after a real response/memory fence. Replies use a bounded
single-producer ledger so the worker never combines one response's context with
another response's status. Stale replies cannot finish the current invoke;
overflow, cookie/visibility failure, link change or timeout terminates and
retains all arena/heap/request memory until actual withdrawal/reset proof.
This replaces donor counter wrap and reconnect heap reuse.

Stop closes the live gate and wakes the original executor's wait. Withdrawal
requires real send/callback/DSP DMA fencing; failure keeps the entire context.
Finish/status/step share a nonblocking executor gate; simultaneous reply callbacks
refuse and hold instead of corrupting the single-producer ledger. Callback storage,
pack table, native context and DMA alias remain retained to reset even after a
successful close. There is no claim that queued messages alone prove stopped DMA.

The pack stays SSCPACK1-compatible but its names/data extents must be canonical,
contiguous, bounded and unambiguous. Embedded NULs, traversal, backslashes,
overlapping names/data and undeclared suffixes refuse. Header byte sum checks
table corruption only; it is not cryptographic authenticity. The actual provider
must verify a same-device firmware/build/registry identity and complete hash
before making immutable reads available. Both original config and persist-sensor
directories plus the original ADSP_LIBRARY_PATH are mandatory; missing/corrupt
inputs never become an empty-pack success. Donor phone calibration is inadmissible.

## Evidence and next integration

Accepted Mac `lab/sensor-rpc-20261119/source7`, `native6`, `kernel4`:
14 actual core/file/binding/metadata/session scenarios plus original pack tests,
15625 checks under ASan/UBSan; transport/DMA/clock/files are modeled. Three
scalar ARM64 objects compile with zero warnings/no initializer. Full kernel
SHA256 `9f694592f957021d31828fc6f11d0ee3afaee64da7f166f1942f5431b744f7ab`,
native `8fc04ebd587a950e87685935cb2497033820c605a489d86dfdbceaeca8edb953`.
Actual provider→listener→file calls, no auto-start, all existing archive owners,
Apple30, motion/pen207/10, Actuator168/186/directIOBuffer, camera/input-sleep/RTC
and motion→haptics→RTC→UFS recovery audits pass. Global experimental flags remain
false. This is a component delivery, not a boot package or hardware proof.

```sh
python3 tools/prepare_sensor_rpc.py --output /new/source
python3 tools/test_sensor_rpc.py --source /new/source \
  --llvm /path/to/llvm/bin --output /new/native
python3 tools/link_sensor_rpc.py --parent /qualified/motion-ssc/kernel6 \
  --rpc-build /new/native --prelink-tool /qualified/prelink-tool \
  --original-kernelcache /original/21H461 --output /new/kernel
python3 tools/audit_sensor_rpc_link.py --kernel-build /new/kernel \
  --parent /qualified/motion-ssc/kernel6 --prelink-tool /qualified/prelink-tool \
  --output /new/kernel/rpc-audit.json
```

Validation is `drivers/EldenMotion/rpc-validation.json`, workbench
`lab/records/sensor-rpc-20261119`; binaries/private inputs stay in
`edl_work/sensor-rpc-20261119`. Mainline must port this to its current shared
ADSP/QRTR/SMMU/power/recovery owners and provide genuine immutable calibration
inputs. It must not overwrite those later fixes with this frozen v152 image.
Next native owned memory/channel/file-provider integration and actual sensor
PD replies; then SSC attributes/samples and the complete J317 SPU/UI rotation
contract. Camera frames/power, system wake, calibrated haptics and RTC persistence
also remain. The five-part Goal stays active.
