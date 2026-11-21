# Native dual haptics — offline link, default off

SE6, the bounded driver chain and root-only IOKit user client now compile and
fully link with the existing pen and RTC components. This is a fixed source-built
v152 offline image, not a current-mainline boot or hardware result. The
source-bound compiled-recipe→RTP path is now plumbed into the same arbiter but
stays disabled without a measured profile; a measured production route and
system Actuator remain open. The five-part Goal stays active.

## Actual platform and worker

`Platform.c` uses only exact TB322FC/SM8750 register/clock/pin facts and the fixed
Linux19.2MHz I2C timing table at100/400/1000kHz. Native starts at400kHz. Active
DMA/GSI and DFS are refused rather than taken over. Known reset pins must already
be GPIO outputs high/deasserted; no reset pulse or IRQ route is written.
Engine/pin/rate/vote readbacks are checked, unknown outside mutation refused.
The actual bus must restore metadata before platform clocks are released.

The revised touch platform and haptics use the same
`sm8750_gcc_vote_owner` transaction protocol. Only their shared-vote RMW operations
hold the guard; neither holds it over I2C/SPI/DMA. Each release keeps wrapper1
clocks enabled while a sibling SE vote exists. The touch contract identifies
this linked source implementation, not hardware readiness.

`Native.cpp` holds one boot-lifetime worker and both real AW instances. No worker
starts without `elden-haptics=1`. It waits for actual root/recovery admission and
checks the actual19.2MHz counter frequency before mapping seven windows. Init
uses gain0/STOP/full firmware SRAM readback, with no waveform playback. Only this
worker touches hardware; user clients enqueue under the actual IOLock.
Stop closes public requests but lets the original worker finish bounded in-flight
transfers and real STOP operations. Unconfirmed stop or bus/platform state retains
the original owner/maps to physical reset. Full suspend/resume is still missing.

## Native protocol and limits

`EldenHapticsService` requires administrator privilege; userspace cannot supply
owner/epoch/gain/boost/registers/firmware. Kernel-assigned sessions use increasing
request IDs. Each of32 mailbox slots reserves its final result, so completion
does not overflow a separate ring. Closing a client cannot release ownership
while its active STOP is unconfirmed.

| Selector | Input | Output |
| --- | --- | --- |
| 0 | none | Eight protocol/phase/readiness/calibration/temperature/error/stop/queue scalars |
| 1 | id, delay-us, duration-us, motor-mask, waveform-index, intensity-Q15 | Queued only; not physical success |
| 2 | request id | Cancel that same session's request |
| 3 | request id | Actual state/error/id receipt; busy while pending |

The optional calibration builder requires exact Gen4 chip/firmware/7V identity,
hashed measured evidence/review notes and bounded per-motor LUTs/budgets. That
validation is not a measurement or safety certification. This delivery has
**no measured calibration**, so playback is refused. Qualified fresh battery
temperature is read from the existing PMIC cache, without another GLINK owner.
Battery temperature and gain-squared budget do not qualify the motor coil or
mechanical response. No synthetic host gain becomes a production limit.

Source-bound recipes are now also carried into that same worker: a build-time
`EldenWaveSource` manifest plus `elden_broker_bind_recipes` admit caller-qualified
parsed recipes under a measured dual-motor `EldenWaveProfile`, and
`elden_broker_submit_recipe` queues a recipe id (`0x80000000`-tagged, so it can
never alias a resident catalog effect id) through the immutable-id mailbox. The
`WaveFeed` pull source then serves the SAME `RtpStream` one bounded 480-sample
chunk at a time — one live prepare+GO session per recipe, one up-front shared
budget reservation, no recipe-sized allocation, staged pulls that neither drop
nor replay frames, and `fed` accounting only on confirmed FIFO writes. This is
capability, not enablement: `Native.cpp` binds no production manifest/profile,
userspace still supplies no PCM/gain/calibration, there is no automatic startup,
and the host test uses explicitly synthetic carriers/LUT only. The manifest's
caller side now has a deterministic generator — `tools/build_wave_manifest.py`
emits sorted/deduplicated `EldenWaveSource` C data from the original Apple
sources plus a qualified bank (test-only synthetic mode stamps
`production_ready:false`; production demands the measured
`hardware_qualified` bank). A real measured
dual-motor profile and an authoritative recipe bank are still absent, so the
route produces nothing.

## Verified scope

Source2/native2/host2/kernel1 are accepted. Actual platform66 cases/3755checks,
touch304168, mailbox2362 and mailbox→queue→GENI→AW91341 checks pass ASan/UBSan.
MMIO/hardware and gains are modeled; the actual IOKit worker/service is compiled
and linked rather than exercised by these portable tests.

Seven library objects are added; only the actual touch platform, pen-preserving
touch worker and UFS recovery member are replaced. Other members/core/30 Apple
execution regions remain checked. The full link preserves service168slots/
6exact overrides and client186slots/7exact overrides, original superclasses,
late metaclass initialization, one real startup call and haptics→RTC→UFS stop
calls. Pen207slots/10overrides and RTC initializer/callback/order checks were
rerun on this image and passed. Global prelink ABI/loadable/runtime/relocation
flags remain false.

Kernel SHA256 `4889eee6af10d10d816f9f8e79704385d264a05c0590cc54cfccec080c21c72c`;
native `3ae88218356370f41d34f6ca5d29c63a5d41d36a948543fbab191029e74df845`.
Archive `edl_work/haptics-native-20261119`:22,492,147B/SHA8f5edc57,180 members
verified. Standalone firmware is excluded; compiled images contain required
original bytes and stay private. Sixteen native source files exactly match the
Git index, recorded at `lab/records/haptics-native-20261119/git-input-proof.json`.
Initial missing-platform-header and test-main-rename failures remain archived.
All jobs are terminal.

## Reproduce and integrate

```sh
python3 tools/prepare_haptics_native.py --firmware /original/y700-gen4-haptic.bin \
  --pen-source /base/pen-source --pen-build /base/pen-native --output /new/source
python3 tools/test_touch_shared_gcc.py --base-native /base/native \
  --llvm /path/to/llvm/bin --output /new/touch-host-native
python3 tools/build_haptics_native.py --source /new/source --base-native /base/native \
  --touch-build /new/touch-host-native --llvm /path/to/llvm/bin --output /new/native
python3 tools/test_haptics_delivery.py --source /new/source \
  --firmware /original/y700-gen4-haptic.bin --output /new/host
python3 tools/link_haptics_native.py --parent /base/rtc-kernel --base-native /base/native \
  --pen-build /base/pen-native --haptics-build /new/native --recovery-parent /base/ufs-wait-trial \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --original-kernelcache /original/kernelcache \
  --output /new/kernel
python3 tools/audit_haptics_link.py --kernel-build /new/kernel \
  --prelink-tool /base/ufs-wait-trial/prelink-tool --output /new/kernel/haptics-abi.json
```

Mainline must port only reviewed source changes to its current touch/SPMI/UFS
and recovery code, rerun ABI/full boot/floor/recovery gates and qualify motor
calibration/temperature/combined load. No tablet, automatic merge or boot package
has been used in this offline branch.
