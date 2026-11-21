# Original input-worker pause and wake — offline

The actual pen nonterminal release and original touch-worker wake coordinator
now compile and fully link. Wake flow executes on the existing touch thread,
not a second SPI worker. The feature remains disabled without
`elden-touch-wake=1` and an installed actual board power provider. No physical
screen/system sleep/wake or current-mainline hardware acceptance is claimed.

## Reversible pen/finger pause

`Delivery.c` adds nonterminal pause/resume. Pause owes an actual HID lift of
tip/range/buttons; failed lift remains paused/pending. Neither new coordinate
nor metadata can reactivate contact while paused. Resume requires acknowledged
release, monotonic clock and absence of terminal stop; it discards saved desired
coordinates rather than replaying the pre-sleep contact. The native pen methods
use the existing service/report sender. They do not recreate HID, clear clock
faults or reset a stopped session.

The touch coordinator releases actual previous finger contacts then pauses pen
delivery. Its normal sampling branch is suppressed for every paused/gesture/
request state. After actual power becomes ready, the same worker drains owned
IRQ, reloads the original NVT firmware with the existing Protocol path, checks
PID`0x6097`/reset state/readiness, releases stale input and resumes pen. The same
touch SPI/DMA/SMMU/maps are retained; actual idle/route observation is required.
Four explicit byte-conversion casts remove current protocol narrowing warnings
without changing framing or command bytes.

## Actual coordinator and kernel power interface

`prepare_touch_sleep.py` derives the reviewed pen-preserving original worker by
exact single anchors, adds `TouchFields.inc` and `TouchCoordinator.inc`, and
replaces only its loop/startup/finish/stop integration sites. Existing haptics
startup, finger/pen normal sampling, SRAM upload and DMA/SMMU implementation are
retained. There is no unqualified board power callback or default success path.

The real display/system power owner must install immutable `EldenTouchWakePower`
with nonzero owner/epoch and actual admit/request callbacks. Normal/paused/
resume/cleanup phases use the existing Flow contract. Paused admission requires
physically retained touch power/transport; resume requires actual panel/rail
readiness. GOP's logical scanout PM does not establish that fact. The provider
must queue its real wake request asynchronously, then submit a power-ready resume
from another thread. Synchronous self-ack from the touch worker is refused.

The offline `EldenWake/PowerOwner.{c,h}` facade now enforces this boundary for
the future provider. Its named owner/epoch and generation fences are atomic and
nonblocking across the touch worker, provider thread and policy lifecycle calls.
It permits one outstanding wake request, accepts only an explicit asynchronous
ready result, and fences duplicate readiness, stop/retire races and stale
rebinds. The facade remains default-off and is not bound by `Native.cpp`.

`elden_touch_wake_request(owner,epoch,asleep,double_tap)` posts one bounded command
under the lock and awaits the original worker acknowledgment, up to2.5s.
Preparation/command/restore errors remain actual codes. Timeout withdraws and
closes the coordinator; it never reports a future success. A pre-transition
power-not-ready refusal remains retryable without treating it as failed SPI.
Locked status snapshots report worker flow separately from atomic command/error
progress; status is not a screen-wake certificate.

Gesture suspend pauses/drains input, sends original`50:13`, waits the OEM50ms
and arms the exclusive GPIO162/PDC109/INTID652 native route. Deep sleep uses
`50:11` and the command event. The actual handler latches/masks652 and wakes the
original thread; only the validated packet's gesture15 submits a power request.
Gesture mode waits on the IRQ event, decoded-request/deep states on command
events, so stale IRQ notification cannot create a full-power polling loop.
After `assert_wait`, only a nonsleeping notification-latch peek is permitted;
provider/route callbacks cannot overwrite the already-prepared scheduler wait.
Pending IRQ/PM/stop rechecks close arrival-before-sleep races.

## Shutdown and retention

Recovery's touch-stop request first wakes the coordinator, then waits for actual
input/IRQ/SPI cleanup before withdrawing terminal finger HID. An unconfirmed
worker stop retains its report sender and maps. A wake-stop error quarantines
the platform; no successful SMMU/clock/map release is invented. Successful stop
retains Context to reset because IRQ/power commands can still reference it, but
clears the actual physical lease only after normal source cleanup succeeded.
Existing haptics→RTC→UFS recovery code remains inherited.

## Evidence

Accepted `lab/input-sleep-20261119/source7`, `native7`, `protocol4`,
`wake-native2`, `coordinator5`, `kernel4`:

-952 decoder/delivery checks include actual nonterminal pause/resume/failed
 lift/rollback/stop behavior.12 actual coordinator scenarios/534 checks pass
 ASan/UBSan with original NVT Protocol firmware restore and actual pen delivery.
 Tests cover no-SPI event wait, gesture filtering/request, real power-ready gate,
 arrival-before-sleep, same-thread refusal, timeout, missing/disabled provider,
 failed upload, failed HID release and stale-IRQ/deep/request wait behavior.
-50 route/1392,12 actual native IRQ/16441 and1233 flow checks pass again.
 Native objects compile scalar; only the inherited touch aggregate warning is
 retained. The current protocol object compiles without new warnings. OS
 scheduler/DMA/GIC/power/HID and SRAM peers are modeled, not physical proof.
-Full kernel SHA256
 `b448a40851b42c44870bcec60c68a94d44bd31aabcd570c6210a7e7bee98bdb2`,
 native `c4081be7391064f9e3af04562fc0d52414d2fb2a8998fe859541865479cbfc80`.
 Actual worker calls/tails bind Flow/IRQ/nonterminal pen/original low-power
 Protocol/scheduler waits; only touch worker/protocol display objects change.
 Pen and wake libraries replace their predecessors once. Apple30, pen207/10,
 motion207/10, Actuator168/186/original IOBuffer and RTC/recovery audits pass.
 Global prelink ABI/loadable/runtime/relocation flags remain false.

Workbench receipt: `lab/records/input-sleep-20261119`. Private archive:
`edl_work/input-sleep-20261119/input-sleep-offline.tar.gz`,21619763B,
SHA256`b8a559c29873f98927ed4540b201e585fc328b46d24cf5c2f91d4da2f0d487b1`,
all159 regular members verified. Standalone NVT firmware is excluded; compiled
native/private bytes remain retained. No tablet access or automatic merge.

## Reproduction and next work

Prepare updated pen HID source over the exact parent, derive touch sleep,
build pen/touch, compile the current protocol and updated wake archive, then
`link_touch_sleep.py` over the camera/motion/wake/Actuator/RTC/pen parent.
Run `test_touch_sleep.py` and component link audits on that exact output. Use
fresh output folders; original source/build identities and commands are in the
verified private archive. Mainline must port to its latest shared input/IRQ/
SMMU/power/recovery code, preserving Wi-Fi/GPU/QRTR/floor gates.

The remaining implementation is the actual board panel/backlight/system power
provider and ready/resume notification, physical wake/IRQ/firmware behavior,
repeated suspend/pen/finger/recovery coexistence. Motion still lacks Gen4 SSC
transport/samples/UI; camera lacks actual power/CSI/VFE/DMA/frames/service;
haptics lacks measured conversion and complete system route. RTC retention/
epoch/alarm remain unverified. The full five-part Goal remains active.
