# AP501U native HID integration — offline, default off

## Current protocol/tilt update, 2026-11-19

`drivers/EldenPen/protocol-validation.json` supersedes the old five-object
milestone below. The actual stock NT36536 instruction fragments now qualify F1
serial/vendor/product packing and the checksum over envelope bytes1..65. F1
retains an existing contact rather than causing a false lift; metadata never
refreshes its stale-contact deadline. This envelope does not checksum pen data
starting at byte66.139 F1/32 envelope instruction cases and the original/fixed
native callback regressions passed with synthetic memory, not a device boot.

The independent report7 is now18B. Its same-report vendor metadata carries
signed tilt and coordinates; the scoped dispatch hook consumes the original
pending message matching the event timestamp. No global latest-tilt cache is
used. The actual OS17 accessor/vptr/expansion offsets were checked against
original bytes:37event/220driver slots, dispatch198, polar type at offset64,
altitude68/azimuth72. The orientation setter field0xb001f is a no-op, so the
qualified hook sets/readbacks the actual type and polar fields. Conversion uses
integer CORDIC/sqrt into IOHID degreesQ16, not UIKit radians. All14641 signed
degree pairs match the host reference within0.00015 degrees. Hardware mounting
and UIKit conversion still require the real probe.

Latest results:932 decoder/delivery checks,15 wire/310 scoped-hook checks, six
scalar ARM64 objects and full pen link2240372e. RTC combined link246ac1ec retains
the same207-slot/10-exact-overrides and late initializer audit. The older signed
UIKit probe/CDHash remains uninstalled. Archive and current remaining work are
listed in [STATUS.md](STATUS.md). The following section describes the original
2026-11-16 delivery; its counts and entry/tar hashes are historical.

The independent pen service, real sampling hook and ordinary UIKit probe now
build. A complete native/kernel link and entry preparation passed over the
source-built fixed v152 baseline; this is **not** a merged current Wi-Fi boot
candidate or a hardware result. Current evidence is
`drivers/EldenPen/hid-validation.json` and workbench
`lab/records/pen-hid-20261116`. All five peripheral objectives remain active.

## Implemented chain

The existing NT36536 worker reads the same109-byte packet. `TouchSource.py`
derives four hooks from its exact source hash, without changing SPI/firmware,
SMMU, controller stop or the finger decoder. Pen processing occurs before the
finger-change test, so pen-only motion is not suppressed by an unchanged empty
finger frame. Transport failure attempts a lift; stop keeps a failed lift visible
to the existing `input_release_error`/return mechanism.

`EldenPenHid.cpp` publishes a separate device, using the same-OS gts7xlwifi
keyboard-first matching method and Stylus application0x0d/0x20. Keyboard/consumer
reports1/2 are never sent; report7 carries the pen. The existing finger HID is
not edited or replaced. Built-In, DisplayIntegrated, usage and maximum pressure
properties are set/read back on the pen's service/provider before a report is
accepted. A registered service and `handleReport` success still require actual
backboardd/UIKit acceptance; no first-read timing guarantee is claimed offline.

The descriptor uses symmetric pressure−4095..4095 (or explicitly selected8191)
with nonnegative hardware samples. The owned event hook converts normalized
IOFixed0..1 into the matching declared UIKit scale1000. It accepts only the
qualified original37-slot event and the pen's own220-slot driver/table. It never
copies the donor's unsafe256 slots, never wraps the finger driver, and refuses a
different table/owner. Unknown pressure values are counted and forwarded without
inventing a valid measurement. The two raw buttons remain barrel usages;
button2 is not falsely treated as an eraser or Pencil double-tap.

Services, copied vtables and their retained owner references live until reset.
Stop performs up to30 retries separated by8ms, reporting actual release state.
That retry count is not a wall-clock bound on an unreturning IOKit workloop call;
existing platform watchdog/recovery remains necessary. Soft restart and full
screen-suspend/resume are not implemented by this first native integration.

`drivers/EldenPen/Owner.c` is a guarded sampling-owner facade the real
worker can bind when mainline is ready (pen-003/004/005). It admits only an
explicitly named provider carrying nonzero owner/epoch and every callback;
`owner_epoch` is rechecked before every facade operation and the frame-owner
identity is sampled per packet before anything reaches delivery. Worker
timestamps and sequence numbers must stay monotonic; owner/epoch drift
observed at ANY facade entry (packet, tick, pause, resume, fault) lifts a
held contact, and a live-generation foreign frame owner or rollback
refusal never decodes the packet but still lifts through the existing
`elden_pen_decode`/`EldenPenDelivery` neutral-release path at the last
accepted monotonic time — a stale/foreign generation mutates nothing.
A failed lift stays pending: `elden_pen_owner_release` retries it through
the bound send with no provider callback, even after stop (which stays
terminal for packets). Stop during an active provider or send callback
defers and unwinds after it returns; retire needs a prior stop and a
confirmed `delivery_released`, then re-arms the caller-owned delivery for
reuse and permits a fresh rebind. `Native.cpp` does not bind the facade;
transport counters plus `pencil_accepted==0` are reported honestly and no
SPI/Pencil/UIKit acceptance is claimed.

The owner facade now uses a single nonblocking atomic whole-operation claim
across the sampling worker, provider callbacks, send/delivery path, stop,
retire and status. Generation-tagged deferred stops are consumed only by the
armed generation; bind refuses generation wrap, and retire reinitializes the
caller-owned delivery only after a confirmed release. WSL and CiscoMac pass
217 single-thread checks and 317 deterministic pthread checks under ASan/UBSan
and TSan, with scalar ARM64 objects clean. These checks remain host-only and
do not establish AP501U SPI, PencilKit, UIKit or backboardd acceptance.

## Verified scope

- 924 prior actual decoder/delivery checks remain valid; those C sources are
  unchanged. New checks cover15 report/pressure cases and249 actual hook/owner/
  forwarding cases with explicit OS peers under ASan/UBSan.
- The pen-003/004/005 owner facade adds217 host checks on synthetic
  provider peers: bind refusals, owner/epoch drift lifting a held contact
  at every facade entry, foreign frame owners and timestamp/sequence
  rollback doing the same on packets, malformed and transport-faulted
  packets, retained failed lifts, an oe-free release retry after a failed
  stop lift, pause/resume gating, callback re-entry (including inside
  `owner_epoch`), stop inside a guard release send, retire refused while a
  lift is owed, and confirmed-retire rebind with old-generation fencing.
  Scalar ARM64 `Owner.o` exports twelve facade entries and imports only
  `elden_pen_decode` plus delivery APIs; `result.json` reports
  `pencil_accepted:false`, `hardware:false`.
- The concurrency hardening adds317 deterministic provider/worker/policy checks
  for packet/stop/send/retire/status races, stale-generation isolation and
  generation exhaustion. `Native.cpp` remains unbound and the facade stays
  default-off.
- Five native objects compile scalar, with no automatic initializer section.
  The pen's metaclass initializer is explicit/late. Four new pen objects have
  no warnings; the touch replacement retains exactly one existing DmaSurvey.h
  aggregate-zero initialization diagnostic. Its exact source/site is checked.
- The actual display archive member is **EldenNvtRuntime.o**, not Runtime.o.
  Only this member is replaced; every other member and original core input is
  checked unchanged. Four new pen calls resolve in the full link.
- Thirty original Apple execution regions remain unchanged. Original HID code
  and207/220/37-slot tables are checked before and after the new link. The ten
  subclass overrides are checked by exact class/method names and native bytes;
  local inline virtuals are included rather than omitted by export-only lookup.
- The UIKit probe builds as ARM64/iOS17 and passes strict4KiB signature checks.
  CDHash1770b67f1baa6d8a461be84b7846daff1701915b. It reads actual touch type,
  force/maximumPossibleForce, altitude/azimuth, timestamps and coalesced counts.
  It does not inject input or require PencilKit/Metal to observe those values.

These are pen-specific ABI/link checks. The legacy prelinker's global
ABI/loadable/runtime/relocation flags remain false and are not relabeled.
Native build2's old aggregate warning rejection and both early audit-script
mistakes are retained in logs/Git history; native3/kernel1 are the final builds.
The audit mistakes were export-only lookup excluding local virtuals, then a
suffix match also selecting metaclass destructors. Exact method checks replaced
both; the linked driver was not changed to make them pass.

## Reproduce without old GPU objects

Supply a source-built OS17 baseline from `tools/build_clean.py` and its native
capture/staged headers (`docs/CLEAN-INPUTS.md` describes original assets). This
run used the separate GPU worktree's freshly source-built v152 baseline read-only;
no GPU object is imported. All outputs below must be new directories:

```sh
python3 tools/test_elden_pen_hid.py --output /new/host
python3 tools/prepare_elden_pen_hid.py --kernel-build /base/ufs-wait-trial \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --output /new/source
python3 tools/build_elden_pen_hid.py --base-native /base/native \
  --derived /new/source --llvm /path/to/llvm/bin --output /new/native
python3 tools/build_elden_pen_probe.py --output /new/probe
python3 tools/link_elden_pen.py --base /base/ufs-wait-trial --base-native /base/native \
  --pen-build /new/native --original-kernelcache /path/to/original-kernelcache \
  --output /new/kernel
python3 tools/prepare_elden_pen_hid.py --kernel-build /new/kernel \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --output /new/postlink-abi
python3 tools/audit_elden_pen_link.py --kernel-build /new/kernel \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --output /new/kernel/pen-abi.json
```

Entry preparation uses existing `prepare_v91_entry.py` with the new kernel hash
and original execution-region table. The tested entry SHA is5e322d72…;
full native3/kernel1/probe1/entry1 records remain under the isolated Mac
`lab/pen-hid-20261116`. Windows `edl_work/pen-hid-20261116/pen-offline.tar.gz`
contains the library, touch replacement, entry kernel and probe; its10731316B
SHA17f29d69… and key member hashes were verified after transfer.

## Remaining work and future trial

Enable only through a reviewed new candidate using `elden-pen=1`; default is off.
Do not blindly append to a1023-byte menu. The explicit pressure override allows
4095 or8191 only, changes the descriptor/decoder limit, and does not reconfigure
the physical pen. Preserve mainline's newer SMMU/Wi-Fi changes when replacing its
touch member, and rerun full boot/trust/entry/AVB/recovery audits. Install/launch
the probe only in that coordinated window; it has not run on the tablet.

The owner facade is source-only: no production caller binds it yet, so the
NT36536 worker→facade→delivery wiring is a mainline decision. The original
finger checksum does not cover the pen. Raw SPI pen capture,0xF1,
tail-byte/integrity semantics and possible scan-mode dependencies remain open.
HID X/Y tilt is retained, but its conversion to UIKit polar altitude/azimuth is
still needed; a descriptor alone does not prove tilt support. Real Pencil type,
pressure, release, matching timing, simultaneous fingers, palm behavior and
Notes/PencilKit must each be verified. No camera/sensor/haptics/RTC result follows
from this pen milestone.
