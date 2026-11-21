# RTP and original Apple compiler — offline, not native output acceptance

Actual AW86937 RTP/controller and two-channel FIFO stream now compile/full-link
with the existing native service, pen and RTC. Original142 vibration recipes and
17AHAP compile through an explicit template/calibration bank. There is no measured
production bank, native RTP request/catalog/shared-budget broker or system Actuator
connection yet. Output stays disabled; the full five-part Goal remains active.

The newer [HAPTICS-BROKER.md](HAPTICS-BROKER.md) supersedes the missing native
request/catalog/shared-budget statement: those are implemented and fully linked
with the real v2 service. Actual measured waveform bank and system Actuator
remain missing; the delivered build still has no output catalog.

## Controller and stream

OEM module f688fcae's actual theory-time map is reg46&60:00=24k,20=48k,40=12k.
FIFO2f..31 thresholds are base/2 and base-base/4 (AE1024/AF1536 at base800);
RTP state8/port32. AFS3/AES4 are almost-full/almost-empty, not exact occupancy or
completion.12 rate/time,3 threshold and512 flag instruction oracle cases pass.

`AwRtp.inc` preserves actual identity/SRAM/gain/owner gates, captures config,
STOPs and verifies standby/base, prepares both chips before GO, writes/readbacks
rate/threshold/mode and restores config only after confirmed STOP. Unknown IO
retains the original chip/bus; no dynamic voltage or raw gain interface is added.

The original GENI packing/command/error state machine stays. Storage grows2→16
FIFO words/64B commands, admitted after actual32-bit/>=16-word FIFO. Writes split
63payload bytes, reads4. No DMA/GSI or unproved refill IRQ is enabled. The change
reduces continuous-transfer overhead; it does not prove a physical1MHz bus.

`RtpStream.c` supports24k clips up to60s, preflights every moving window's
sample/on-time peaks and measured complete-path bandwidth with25% margin, uses
a mandatory persistent-budget reservation callback and bounded fair refill.
Early standby/IO/session/clock/service-gap errors stop. Complete requires all
bytes fed plus DONE+standby; failed/cancelled/held remain distinct. Failed stop
retains active bits. The current native reserve/request provider is still absent.
Dual24k is not justified merely by a400k I2C clock.

## Compiler

`EffectCompiler.py` preserves source atom type/start/duration/period/cycles,
master/event intensity, explicit envelope units, template loop/tail and AHAP
intensity/sharpness automation. It invokes the actual C `SampleMap` for signed
per-motor conversion and rolling budgets. Unknown/mismatched templates, frequency,
StartingGain, fractional-cycle policy, clipping and budget excess reject rather
than receive an invented carrier/amplifier. AudioCustom is never motor PCM.

Bank identity binds TB322FC/firmware84598d2a/7V/24k, hashed measurement/review,
per-motor LUT/gain/window limits and explicit morph/dynamic/repeat policy. Duplicate
keys reject. Production CLI refuses synthetic banks. Actual D83 f50d5002's142
recipes/20 atom types and17AHAP pass with explicitly SYNTHETIC template/gain peers.
These tests prove compiler plumbing, not calibrated Taptic equivalence. Real
template/physical gains and source-envelope/legacy policy qualification remain.

`WaveCompiler.c` is the source-side continuous counterpart: it carries a parsed
recipe (ordered events plus intensity/sharpness automation) to the two channels
as bounded480-sample timestamped RTP chunks under a caller-fed contract. Each
chunk converts through the calibrated `SampleMap` LUT for BOTH motors and enters
the moving dual-motor window (squares, motor-on, simultaneous dual-on) before it
is emitted. Output requires an explicit measured dual-motor profile — absent,
disabled, stale-revision, out-of-range or single-motor coverage all refuse; the
conservative default stays disabled. Each motor's LUT is re-qualified at
admission through the shared `SampleMap` validator (anchored endpoints, strictly
increasing magnitudes, nondecreasing drive bounded by peak, supported rate/
max_frames, nonzero budget), and an explicit tail must fit inside its event —
a shorter duration refuses rather than underflowing into silent loop playback.
Armed runs are generation-stamped; produce
requests must land on the exact next source position so a skipped/gapped or
already-served frame is a timing/underrun refusal, and a stop during a pending
feed becomes a recorded partial abort, never false-complete. Intensity and
sharpness stay perceptual conversion inputs — no voltage/boost/resonance/duty
or thermal limit is invented and no carrier/amplifier is inferred. Host tests use
a SYNTHETIC linear-LUT profile: they prove the refusal/contract plumbing and the
exact conversion math, not measured motor equivalence. Real measured
calibration, an authoritative recipe bank and physical acceptance remain
missing.

`WaveFeed.c` now carries the compiled chunks the rest of the way: it is the
pull source that connects `WaveCompiler` to the SAME serialized native
Broker/RTP worker (`RtpStream`/`Broker`) a resident catalog clip already uses.
A build-time `EldenWaveSource` manifest binds source recipe ids (tagged
`0x80000000` so they can never collide with a catalog effect id, and `Catalog`
rejects that bit) to caller-qualified parsed recipes; `elden_broker_bind_recipes`
then admits one manifest plus a measured dual-motor `EldenWaveProfile` and a
caller-owned rolling-history ring. `elden_broker_submit_recipe` queues a recipe
id through the existing immutable-id mailbox/session/worker path — never PCM,
gain, boost, calibration, register or voltage, which userspace cannot supply.
On dispatch `start_recipe` arms the `WaveFeed` (the measured-profile admission
still refuses absent/disabled/stale/out-of-range/single-motor coverage) and
hands `RtpStream` a pull-source clip: `clip.pull` serves each motor's next
≤64-sample window from ONE bounded resident chunk at the exact expected source
position, `peak_squares` reserves the producer's verified worst moving-window
cost once for the whole streamed recipe. One live prepare+GO session spans the
full recipe — no per-chunk STOP/GO restart, no recipe-sized allocation, and the
shared budget reservation is made once, not per 20ms chunk. Consumption is
two-phase: `clip.pull` only STAGES bytes into the worker's per-motor `feed`
buffer (advancing the `pulled` cursor), and `RtpStream` invokes
`clip.consumed(motor, first_frame, count)` only after `elden_aw_rtp_feed`
succeeds for those exact bytes. Staged bytes persist across `EldenAwBusy`
unchanged and retry exactly once — no repull, no duplication; `WaveFeed` keeps
the chunk resident until BOTH motors' `confirmed` cursors reach the chunk
length, and only then calls `compiler_fed` and compiles the next chunk. A
cancel or stop with staged-but-unconfirmed bytes therefore drops them honestly
(`dropped`/`pending` account for them) instead of counting unwritten samples as
drained. Missing source carrier, partial coverage,
budget/timing/FIFO failure, cancellation, stop and stale generation all refuse
or settle through the existing broker uncertainty/ownership rules. `Native.cpp`
still binds no production manifest/profile — no measured dual-motor profile
exists, so the production/default route stays disabled and there is no
automatic startup path. Host `TestWaveFeed` drives the real GENI/AW peer chain
with explicitly SYNTHETIC carriers/LUT: ordered no-dropped/no-replayed FIFO
bytes, simultaneous GO, refusals, cancel/stop, and the staged→confirmed
contract — staged-then-FIFO-busy + cancel drains nothing, one confirmed motor
with a busy sibling never retires the chunk, and a busy retry writes the staged
tail exactly once. Measured calibration and physical acceptance remain
missing.

`tools/build_wave_manifest.py` is the deterministic offline generator for that
caller manifest: it parses the original `SystemSoundVibrationPatterns` plist
and supported AHAP through the existing `AppleEffects` parser, qualifies every
carrier against the measured source-bound bank via `EffectCompiler.load_bank`,
and emits bounded static C — deduplicated 24 kHz sample arrays, `EldenWaveCarrier`
(one-shot/loop/tail), `EldenWaveEvent`/`EldenWaveAutomation` (intensity and
sharpness steps/curves), `EldenWaveRecipe` and a sorted `EldenWaveSource` table
whose ids are stable from source name+hash, nonzero, unique and free of the
`0x80000000` recipe tag. Only semantics `WaveCompiler` already represents are
emitted: one-shot/loop/tail carriers, AHAP transient/continuous events with a
single sharpness morph pair of identical layout, intensity/sharpness
automation, and VibePattern segments only under an explicit qualified legacy
carrier/order. Unsupported waveform mappings, >2-point sharpness morphs,
mismatched morph carriers, unresolved duration, envelope units, fractional
repeats, audio sidecars, duplicate names/ids and unqualified dynamic policy all
refuse; no Tap/Sine carrier is ever synthesized and no intensity->voltage is
inferred. A source set yielding no supported non-empty recipe refuses rather
than emitting a zero-entry manifest, and distinct curve point times that
quantize to the same 24 kHz frame refuse in both the generator and
`WaveCompiler.valid_curve` (strict point ordering — equal frames would divide
by zero in `curve_at`). Production mode requires the bank's `hardware_qualified` evidence,
exact TB322FC board/rate/boost and pinned firmware identity; `--test-only`
admits a synthetic bank and stamps `production_ready:false`. The generated TU
is host-qualified by `TestWaveManifest` (5,821 checks: sorted/unique/untagged
ids, carrier dedup, morph/automation contract, every recipe drained through
WaveCompiler and the staged->confirmed WaveFeed) and builds a scalar ARM64
object on both toolchains. Nothing links it into `Native.cpp` — the manifest
remains source-derived capability, not enablement.

## Evidence and reproduction

Final RTP host3:361674 actual modeled GENI/AW/RTP checks including60s admission/
setup/stop without a60s sleep. Bus/delivery regressions and nine native objects
pass. Kernel14f945b5/nativec54fd2fb preserves Apple30, service168/client186, late
initializers and actual stop→RTC→UFS calls; pen207/10 and RTC audits pass again.
Global ABI/loadable/runtime/relocation flags remain false. Native links the RTP
library; the source-bound compiled-recipe path now reaches it through the
bounded WaveFeed pull source, but `Native.cpp` still binds no production
manifest/profile, so no route is enabled and there is no automatic startup.

`rtp-validation.json` and workbench `lab/records/haptics-rtp-20261119` hold compact
evidence. Private archive21,828,210B/SHA94e011f5 has276 verified members, excludes
standalone firmware/synthetic templates; native includes required original bytes.
Large full prelink lists stay Mac-local.14 source inputs/generator replay match Git.

```sh
python3 tools/prepare_aw_chip.py --output /new/source
python3 tools/test_haptics_rtp.py --source /new/source --firmware /original/y700-gen4-haptic.bin \
  --llvm /path/to/llvm/bin --output /new/host-native
python3 tools/test_effect_compiler.py --vibration /original/patterns.plist \
  --ahap-dir /original/Patterns --output /new/compiler-host
python3 tools/test_wave_compiler.py --llvm /path/to/llvm/bin --output /new/wave-host
python3 tools/test_wave_feed.py --firmware /original/y700-gen4-haptic.bin \
  --llvm /path/to/llvm/bin --output /new/wavefeed-host
python3 tools/compile_apple_effects.py --source /original/patterns.plist --recipe NAME \
  --bank /measured/bank.json --sample-map-library /host/libSampleMap.dylib --output /new/effect
python3 tools/build_wave_manifest.py --source /original/patterns.plist \
  --source /original/effect.ahap --bank /measured/bank.json --output /new/manifest
python3 tools/test_wave_manifest.py --llvm /path/to/llvm/bin --output /new/manifest-host
```

Full native uses existing prepare/build/link_haptics tools; native-source2/native2/
kernel1 accepted. The source-bound compiled-recipe path is now plumbed into the
same arbiter/worker but stays disabled without a measured profile, and the
deterministic manifest generator (`build_wave_manifest.py`) exists but has no
production bank to consume. Still needed:
a real measured dual-motor calibration bank (`hardware_qualified` with
evidence), a production manifest generated from it, an explicit `Native.cpp`
bind, actual iOS Actuator for the RAM path, then port/rebuild current
mainline with floor/recovery. No tablet or auto merge.
