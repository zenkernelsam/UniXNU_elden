# Gen4 camera control — source-derived offline core

The front/rear sensor control core now compiles for ARM64/iOS17. It reuses the
actual fixed TB322FC Linux6f97250e S5KJNS and GC08A8 tables, power/control/stream
bodies. Subsequent exact native CCI/control binding is now implemented/full-linked
in [CAMERA-CCI.md](CAMERA-CCI.md). Actual power, CAMSS/CSI/VFE/SMMU/DMA and OS17 camera service remain to connect;
no actual sensor response or camera frame has been captured here.

## Source scope

Rear reference: S5KJNS at7-bit0x56/CCI0, expected ID0x38ee read at register0x0000,
4080×3072 GRBG RAW10,672MHz link/558933333 pixel-Hz, resetGPIO11, MCLK3/19.2MHz,
rotation90. Front reference: GC08A8 at0x31/CCI1, expected ID0x08a8 at0x03f0,
3264×2448 RGGB RAW10,336MHz link/281600000 pixel-Hz, resetGPIO7, MCLK4/19.2MHz,
rotation270. These identities/wiring describe the selected source, not observed
Y700 hardware. Armada's README explicitly qualifies camera DT control nodes only.
Board resources/pins/clock/supply identity must be verified before live power.

`Source.py` preserves325 rear and438 front register entries and all eight
power-on/power-off/control/stream function bodies. The explicit adapter replaces
Linux framework operations with the core's real owner/refcounts/serialization
and mandatory external CCI/power/receiver callbacks. It does not emulate Linux,
publish an IOKit camera or provide successful placeholders for missing providers.
Repeated Samsung0x6f12 data-port writes retain their individual register addresses.

Actual ID reads occur after owned power-on and before configuration. Regulator/
clock enable errors stay in the cleanup ledger because an unsuccessful call may
have physically applied. Group hold is released even after a control-write error.
An attempted/failed stream-on remains visible until real receiver drain and power
stop; a software streaming=false alone is insufficient. Drain failure refuses
power release and retains resources. Recovery retries use the actual ledger,
not fabricated runtime PM refs. The core cannot release a peer's unowned refs.

Public modes are **expected** descriptors; they do not prove streaming. The
terminal result covers this owned sensor/receiver control context, not camera
frames, a global CAMSS controller or a system-return acceptance.

## Offline validation and remaining work

Source2/host2 passes820 actual extracted-function scenarios/882646 checks under
ASan/UBSan, including every stream-table/control write failure, power errors,
wrong ID, reentry, missing receiver readiness, failed drain and group release.
Hardware/power/CCI/CAMSS are explicit modeled peers. One warning-free scalar
ARM64 object builds. The first host signedness failure is preserved; the adapter
uses an int count for these fixed small arrays without changing source bodies.
Evidence: `drivers/EldenCamera/validation.json`, workbench
`lab/records/camera-core-20261119`; native object in corresponding `edl_work`.

```sh
python3 tools/prepare_camera_core.py --output /new/source
python3 tools/test_camera_core.py --source /new/source \
  --llvm /path/to/llvm/bin --output /new/host-native
```

CCI ownership/controller and actual core/native binding now have offline code
and full-link evidence in CAMERA-CCI.md. Next exact-board rails/pins/clocks, CSI/CSIPHY/
VFE/DMA/IRQ and immutable SMMU route, real buffers/RAW10/debayer/controls, then
OS17 camera delivery. The T970 OS17 camera extension is a possible interface
reference; its SM8250 camera registers are not Gen4 wiring and its README does
not claim app acceptance. No synthetic frames replace the missing pipeline.
No boot image, tablet access or automatic mainline merge has occurred. Goal active.
