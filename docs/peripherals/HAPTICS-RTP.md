# RTP and original Apple compiler — offline, not native output acceptance

Actual AW86937 RTP/controller and two-channel FIFO stream now compile/full-link
with the existing native service, pen and RTC. Original142 vibration recipes and
17AHAP compile through an explicit template/calibration bank. There is no measured
production bank, native RTP request/catalog/shared-budget broker or system Actuator
connection yet. Output stays disabled; the full five-part Goal remains active.

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

## Evidence and reproduction

Final RTP host3:361674 actual modeled GENI/AW/RTP checks including60s admission/
setup/stop without a60s sleep. Bus/delivery regressions and nine native objects
pass. Kernel14f945b5/nativec54fd2fb preserves Apple30, service168/client186, late
initializers and actual stop→RTC→UFS calls; pen207/10 and RTC audits pass again.
Global ABI/loadable/runtime/relocation flags remain false. Native links the RTP
library but does not yet route a user request to it.

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
python3 tools/compile_apple_effects.py --source /original/patterns.plist --recipe NAME \
  --bank /measured/bank.json --sample-map-library /host/libSampleMap.dylib --output /new/effect
```

Full native uses existing prepare/build/link_haptics tools; native-source2/native2/
kernel1 accepted. Next persistent native RAM/RTP arbiter, source-bound catalog and
session requests, actual iOS Actuator. Calibrate motor/temperature/dual-load and
then port/rebuild current mainline with floor/recovery. No tablet or auto merge.
