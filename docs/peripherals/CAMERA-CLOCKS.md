# Gen4 camera clocks, dedicated pins and board lifecycle

The camera path now has a source-derived CAMCC/CAMBIST clock graph, actual
native MMIO adapter and board lifecycle that connects clocks/reset to the
existing WL2868C supply, sensor, CCI and CSIPHY/CSID/VFE implementation.
It builds and links over [CAMERA-POWER.md](CAMERA-POWER.md). No tablet was
accessed, no real resource provider is automatically installed, and no camera
frames or iPadOS camera service have been accepted.

## Source and shared dependencies

`clock-source.json` pins20 files from Armada's exact TB322FC/SM8750 Linux
`6f97250ead155fc3b980856323e99a4623dae73d`. `ClockSource.py` extracts23 original
RCG/branch/GDSC/GPIO helper bodies and the actual clock aliases, parent maps,
frequency tables and per-PHY/CSID/VFE resource lists. Helper changes are limited
to internal symbol visibility and binding delays to their own context. The
original source and copyright/license notices stay with the imports.

The generated graph has39 clock IDs,21 RCGs,37 local branches and4 domains.
Clock IDs are the original35 CAMSS DT names plus CCI0/1 and MCLK3/4. Full/lite
VFE resource lists come from the original CAMSS source; the board adapter
requires an explicit complete source-supported frequency plan. Missing entries,
unsupported rates, wrong sensor mode/RDI/epoch and duplicate owned decoder are
rejected before physical acquisition. A listed frequency is a supported source
fact, not a recommendation that bypasses actual parent/domain qualification.

| Native window | Address and extent | Ownership |
| --- | --- | --- |
| CAMCC |`0xade0000`,0x20000 |Original exclusive camera-clock provider |
| CAMBISTMCLKCC |`0x1760000`,0x6000 |Same original provider |
| TLMM camera span |`0xf100000`,0x78000 |Only GPIO7/11/92/93/113/114/117/118 may be written |

The native map does not claim ownership by itself. Before any MMIO, the original
provider must hold real GCC interface/NoC, RPMh MMCX+MXC/MX votes, dedicated pins
and actual current/safe-source parents. Its shared sleepable lock serializes
clock/TLMM changes with peer owners. Callback contracts forbid reentry into that
lock or core. There is no independent clock controller, blind shared PLL setup,
GPIO IRQ rewrite, PMIC126 reset, backlight HWEN88 change or I2C8/9 takeover.

CCI0/1 use37.5MHz, the exact PLL0even600MHz/select2/div16 source recipe.
MCLK3/4 use19.2MHz XO/select0/div1. PLL selectors are qualified separately per
root: equal numeric selector IDs on different CAMCC parents do not mean the
same PLL. Every dependency request carries the actual root/leaf identity,
requested frequency and original CFG/M/N/D snapshot. The original provider
acquires/observes real parent clocks and supplies their actual rate. Unknown or
missing rates refuse; the source's one-Hertz fractional-divider rounding is
explicitly bounded and actual observed frequency is returned.

## Clock, domain and pin failure handling

`Clock.c` uses the original shared RCG force-enable, update, clear-force and
safe-source parking helpers. Original500×1µs RCG and200×1µs branch bounds remain.
All read/write/gate errors are latched across helpers whose upstream boolean or
void signatures cannot propagate failure. After void branch disable, actual halt
status is checked again; enable-bit0 alone cannot retire a running branch.

Attempted parent/leaf references are recorded before operations. Shared root
rates must agree before a new parent request; conflicting requests cannot
perturb another active camera. Originally enabled matching clocks remain enabled
when their temporary borrower exits. Before changing/restoring a shared root,
the original provider must prove its external users idle; a local enable bit is
not a consumer or DMA proof. Unknown CFG/M/N/D changes refuse. Actual shared-disable parks the entire CFG,
so final restoration replays and verifies its complete original32-bit snapshot;
foreign changes to its other fields cannot be silently discarded. A failed readback
or restoration retains reference/dependency state for cleanup or full reset.

GDSCs use actual CFG power-up/down status at control+4, not genericbit31. Original
transition wait fields and flip-flop retention are used. Startup rejects
contradictory/transitioning/HW override status rather than assuming a stable
domain. Originally enabled domains are preserved. Last child-domain withdrawal
requires all clock references retired; child TFE domains retire before common
TITAN_TOP. Actual original domain-unused and sensor-idle proofs remain mandatory.

GPIO pins are snapshotted before the first change. CCI113/114/117/118 uses
function1/2mA/pull-up; MCLK92/93 function1/2mA/no-pull; resets7/11 GPIO output,
active-low. Original eGPIO present11/enable12 ownership is respected. GPIO output
latch is set before output-enable. Final restoration of an originally input
reset pin disables output before restoring its old latch, avoiding a transient
reset deassertion. Recognized partial states permit cleanup; unknown peer changes
retain the pin lease. GPIO interrupt configuration is read for admission and
never written.

`NativeClocks.cpp` and `NativeClockMethods.inc` apply that core through real
scalar LDR/STR, barriers and19.2MHz counter time. Register access is limited by
the generated graph and dedicated pin membership. Initialization, mapping and
core contexts are one use and retained to reset; no callback can reopen a
faulted controller or drop uncertain mappings.

## Real board composition

`Board.c` connects the actual native clock graph to the existing powered capture
wrapper. It acquires the original receiver-resource lease, common TITAN_TOP/
selected full-VFE domain, the exact per-instance clock lists and CCI, then opens
the native CCI/receiver and binds the source-derived sensor. MCLK and reset calls
now reach the actual native clock/pin core, while supplies still reach the one
shared WL2868C/I2C10 session.

Receiver resources must come from the original shared owner: actual PHY3/4
0p88/1p2 rails/load, interconnect votes, early CAMSS SID1c00/SMMU route, wired
buffers and actual physical DMA/IRQ retirement. `receiver_resources` must retain
all external/safe clocks through local shutdown and retire only after observed
stop. No new success stub or frame generator supplies these inputs.

Normal requests require the actual board/clock gate to stay ready. If an earlier
clock or drain fails, only the serialized `board_stop` scope admits original CCI
sensor STOP writes using actual retained **cleanup-only** clock observation.
That observation does not revive normal startup or change the fault latch.
The composed failure test verifies both cameras are already started before one
drain fails; the other camera's supply/clock references remain protected.

Stop each sensor/physical receiver, restore its supplies, stop CCI and withdraw
its local clocks. The combined shutdown stops both, releases child TFE domains
before TITAN_TOP, retires receiver resources, then closes the WL session and
restores final pins/controllers. Failed stop/restore preserves the appropriate
lease to reset. The current offline kernel does not add an outer board start or
change the established platform recovery object/deadlines. Mainline must review
and connect this explicit lifecycle to its current shared owners and recovery.

## Offline evidence

Accepted Mac folder: `/Users/ciscohe/Desktop/UniXNU_elden-offline-peripheral-fixes/
lab/camera-clocks-20261120`.

- `host17`:1815 scenarios/1828185 checks exercise actual helper/core code under
  explicit physical/MMIO/parent/RPMh/owner models, including every representative
  admission/read/write/delay failure, writes that applied before error, original
  enabled states, two-camera references, external changes, true polling bounds,
  incomplete GPIO retirement and cleanup-only CCI clock observation.
- Original native methods execute under host IOKit/MMIO peers in normal, mapping
  refusal and interrupt-context refusal cases. No real ownership is inferred.
- `stack7`:both targets prepare and run through real Board→native clocks→WL→
  sensor→CCI→CAMSS. Normal30170 and failed-drain30336 checks pass; explicit bad
  rate plans are refused before receiver acquisition. Physical callbacks, MMIO,
  IRQ and device DMA remain modeled; the tests produce no real camera pixels.
- `native6`:three scalar ARM64/iOS17 objects build without compile warnings or
  automatic initialization. `kernel5` fully links (kernel
  `3f59efa02586b309b13518312e0445d81f472560d0bcd466dd9269a138b983eb`,
  native `12b7da547d86efb83b04610d7b105e5bb5afb5c235955bc4c9f84939e117ec41`); all original archives and
  recovery members stay identical to the prior shared-power image, all30 Apple
  executable ranges are preserved, and the existing peripheral/link audits pass.
- 133 exact source/topology/helper checks and17 actual-function startup rejection
  cases pass. The prior32 peripheral guards and19 shared-power archive/function
  guards remain verified. Global ABI/loadable/runtime/relocation flags stay false.

Use `prepare_camera_clocks.py`, `test_camera_clocks.py`,
`test_camera_board_stack.py`, `build_camera_clocks.py`, `link_camera_clocks.py`,
`audit_camera_clocks_link.py`, `audit_camera_clock_sources.py` and
`test_camera_clocks_audit.py`. Required paths are in each `--help`. Parent is
source commit`dbb7115`/CAMERA-POWER kernelcbad4612. Accepted stack inputs remain
camera pipeline source9, CCI source7, sensor-core source2 and shared-power source3.
`clock-validation.json` records final hashes/commands and private archive;
workbench `lab/records/camera-clocks-20261120` carries the compact receipt.

Next supply the real shared original RPMh/GCC/parent/CSIPHY owner and early
exclusive camera DMA/SMMU allocator/retirement proof, then real RAW10,
calibration/ISP and OS17 camera service. No hardware or whole-five-part Goal
acceptance follows from these offline checks; all five objectives remain active.
