# Native RAM/RTP requests and shared budget — offline

The actual native service now uses one broker for both RAM clicks and immutable
RTP effects. Its source, shared budget, catalog builder, client handlers and full
kernel link are checked offline. The delivered catalog/calibration remain absent
and output disabled. The subsequent actual OS17 HAL/native Actuator source and
offline full link are in [HAPTICS-ACTUATOR.md](HAPTICS-ACTUATOR.md). Real iOS system
route, measured Taptic conversion and hardware acceptance remain incomplete;
the whole five-part Goal stays active.

## Connected execution path

The existing native IOLock protects only mailbox/session changes. The retained
worker runs `Broker.c` and owns both actual GENI/AW chips. RAM and RTP share request
IDs/results, ordered dispatch, cancellation, disconnect and terminal stop. Native
RTP prepares both motor configurations before GO, then fairly feeds retained
immutable buffers. Failed STOP holds owner, pending receipt, PCM and budget to
physical reset. New owners cannot take resources from a closing/failed session.

`Budget.c` persists across client reconnects and both playback forms. It reserves
moving-window worst gain²×signed-sample² and motor-on time, not an assumed coil
model. RAM uses conservative peak127. RTP zero crossings do not mean motor-off;
the whole streaming interval counts. Active/unconfirmed entries never expire
from time alone. Confirmed STOP extends the reservation through the latest
end+window. Backwards clock latches refusal. This is boot-lifetime software
accounting, not persistent storage or a motor safety certificate.

Queue-local budget remains, with an actual shared reserve callback before GO.
Completion updates shared accounting only after physical stop/done. Fresh time
after previous STOP prevents a stale queue timestamp from looking like rollback.
No raw PCM/gain/voltage/register upload or caller-selected owner/epoch is exposed.

## Version2 user client

| Selector | Input | Result |
| --- | --- | --- |
| 0 | none | Eight protocol/state/calibration/temperature/error/stop/RAM-ready scalars |
| 1 | id, delay-us, duration-us, mask, RAM waveform, intensity-Q15 | Queued RAM request |
| 2 | id | Same-session cancellation |
| 3 | id | Actual final state/error/id; busy while pending |
| 4 | id, immutable effect-id, delay-us, intensity-Q15 | Queued RTP catalog effect |
| 5 | effect-id | id/frame-count/mask/rate metadata; no user gain or PCM |

Type0 remains administrator-only; the separate system type1 route is described
in HAPTICS-ACTUATOR.md. Handler input counts/descriptors/narrowing,
owner close races and failed-output preservation use the actual code in
`ClientMethods.inc`; native and host peer tests include exactly the same methods.
Selectors0..3 keep their shapes. Service property reports version2/catalog count.
Default catalog is empty, so effect submission refuses; it never uses a test
bank as a fallback. Native qualified fresh temperature/recovery gates are retained.

## Catalog and production inputs

`NativeInputs.py` binds board/chip/firmware7V, native-calibration digest, original
Apple source, measured template bank/evidence, complete-path bandwidth at actual
bus rate, per-motor gains/rolling limits, mapper/parser/compiler identities and
every output PCM digest. It replays the actual source/compiler and compares
every byte, rejecting a forged but self-consistent manifest. Data becomes an
immutable32MiB-bounded native catalog; validation does not certify measurements.

Native preparation accepts `--calibration` and optional `--catalog`. The catalog
JSON selects increasing IDs, name, compiled manifest/hash, original `source_path`,
`bank_path` and `sample_mapper_library`, all relative to the catalog file. It
requires source/template/bandwidth evidence and reviewed native limits. The
actual compile tool writes its compiler/parser/mapper-source/library hashes.
No input flag substitutes for missing real evidence. An empty default produces
no max gains, no catalog and no measured bandwidth claim.

Positive generator schema tests are explicitly synthetic HOST-ONLY format peers,
used in a host structural C++ executable. They are not native build/boot inputs,
not published as calibrated records and excluded from the delivery archive.
Negative cases cover qualification/evidence/hash/duplicates/manifest/PCM/bank and
source replay. No real motor or physical bandwidth was measured in this branch.

## Offline evidence and handoff

Accepted source4/native4/host6/inputs3/kernel2:

-151 actual budget checks,283066 composed broker/queue/mailbox/GENI/AW/RTP checks,
 98 actual service/client handler checks under ASan/UBSan with physical/IOKit peers.
-RTP362399 regressions include read-clear DONE latch, almost-empty never completes,
 zero PCM still counts motor-on,60s admission/setup/stop. Nine catalog negatives
 and actual generated C/C++ catalog/budget structure checks pass.
-Twelve new/replacement native objects compile scalar; old touch aggregate
 diagnostic remains. Full link kerneld64231c6/nativeb6a72095 preserves30 Apple
 execution regions, service168/client186 exact overrides, late initializer and
 stop→RTC→UFS actual calls. Pen207/10 and RTC callback/order checks pass again.

Global ABI/loadable/runtime/relocation flags remain false. Default-off/no measured
catalog is the full-link evidence; host positive schema data was never used for
that build. Windows/Mac compiled23 source files and generator replay match Git.
Private archive25,996,657B/SHA5c704b1f has523 verified members; standalone firmware,
synthetic positive inputs and huge prelink lists excluded. Native necessarily
embeds the required verified firmware bytes. Evidence:
`drivers/EldenHaptics/broker-validation.json` and workbench
`lab/records/haptics-broker-20261119`. Earlier failed host fixtures/compiles retained.

```sh
python3 tools/prepare_haptics_native.py --firmware /original/y700-gen4-haptic.bin \
  --pen-source /base/pen-source --pen-build /base/pen-native --output /new/source
python3 tools/test_haptics_broker.py --source /new/source \
  --firmware /original/y700-gen4-haptic.bin --output /new/host
python3 tools/test_haptics_inputs.py --output /new/schema-host-only
python3 tools/build_haptics_native.py --source /new/source --base-native /base/native \
  --touch-build /base/qualified-touch --llvm /path/to/llvm/bin --output /new/native
```

Link/audit uses existing `link_haptics_native.py`, haptics/pen/RTC auditors. Mainline
must port reviewed source to current touch/clock/recovery, then qualify measured
motor/template/gain/thermal/bus behavior and the actual system Actuator/capability/
preset route. No capability switch is advertised before that service exists.
Camera/motion native providers and system wake also remain incomplete. No tablet,
automatic merge or alteration of mainline Wi-Fi/QRTR/GPU work occurred.
