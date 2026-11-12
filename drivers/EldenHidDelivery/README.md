# Reliable Elden touch delivery — offline component preparation

This component work is now included in the separately audited, signed
[v100 candidate](../../profiles/v100-hid-delivery/README.md). It remains unflashed.

Two failures are reproduced from the exact v94 input source, without a tablet:

- An allocation failure on the final lift updates `lastSeq` and clears the old
  contact before `handleReport` succeeds. The void callback reports success to
  the board producer, and even a subsequent empty frame cannot release the ID.
- One producer can overwrite another producer's snapshot before its synchronous
  work-loop action consumes it. The original controlled down/up interleave
  drops both reports.

The current reference ShengHidInput at
`114061452959a547aaa08f9ca5aa54033394ca2f` still updates the contact history before
submission and uses a void notification. Its successful device work remains the
HID architecture reference; these error/interleave cases need an Elden adapter.
This does not establish that either case caused the owner's earlier symptoms.

## Change

`SourcePatch.py` checks the exact tested NativeHidInput and EldenHidGlue inputs
and derives two new units. The frozen profiles are not edited.

Contact history advances only after an accepted report. The versioned
`elden_touch_set_sink_v2` callback carries the real IOReturn through the work loop
and board publisher. Both units must be built together; the old void callback
cannot satisfy the new symbol. The existing physical producer already retains
its previous frame on a nonzero publish result and reports failed cleanup.

A separate sleepable publication lock orders physical/software producers across
snapshot publication and callback completion. `sinkLock` is released before the
callback enters the I/O work loop. No IRQ handler takes the publication lock;
these producers are worker threads. No idle polling or periodic timer is added.

Only NoMemory, NotReady and Busy results are retried, at most four attempts with
three8ms sleeps **outside** the I/O work-loop gate. This bounds explicit sleep
to24ms, not total IOKit execution time. Other errors, or exhausted retries, reach
the producer. Absolute-contact retries do not establish exactly-once delivery
when an underlying driver partially processes an error-returning report.

This does not implement service teardown/restart, suspend/wake, gesture
calibration or every possible concurrent lifecycle. The existing frozen profile
does not unload the digitizer at recovery; physical shutdown can still send its
final lift. Coordinates, IDs, report descriptor, event ABI and controller code
are unchanged. No charging, wireless, sound, GPU or boot options change.

## Evidence

See [the record](../../docs/evidence/hid-delivery-20261112.json).
The host test extracts the actual old and derived methods and publisher, then
substitutes IOKit allocation/report dispatch and locks. Only the two ARM memory
barriers are replaced with host fences; this is not a kernel or hardware emulator.

ASan/UBSan tests cover transient/exhausted allocation, missing event driver,
`handleReport` failure, absent sink, invalid input, redundant empty frames and
ordered concurrent down/up. Five normal multi-touch reports have the same byte
fingerprint before/after (`551842de3d172b8c`). The original failures reproduce;
the repaired cases pass. A WSL NAT diagnostic was preserved separately from the
test output; there were no sanitizer errors.

Both ARM64/iOS17 objects compile with the captured kernel headers/ABI and are
scalar. Export checks allow only the callback's explicit rename. The original
late metaclass initialization still removes `__mod_init_func` and globalizes
`__GLOBAL__sub_I_NativeHidInput.cpp`, exactly as its parent build did. This is
not a new automatic kernel initializer.

Windows records: paired workbench `lab/records/hid-delivery-20261112/`.
Mac final result: project-local `lab/hid-delivery-20261112/native3/`; earlier
failed builder attempts remain there. Later v100 link/boot records are separate
from this component audit. v94 and v95/v96/v99 remain unchanged.

## Reproduce

Python3.12, run from this source repository:

```sh
python3 tools/test_hid_delivery.py --compiler c++ --output /path/to/new-host-output
python3 tools/build_hid_delivery.py \
  --native-parent /path/to/verified-v91-native-output \
  --llvm /path/to/llvm/bin --output /path/to/new-native-output
```

The native step requires macOS and the original captured Xcode/iPhoneOS context.
For Windows host tests, select `--compiler g++ --wsl-distro Ubuntu-22.04` and put
the new output under Windows TEMP. Tests never open the device. The next build
must link both new objects and recheck the complete image before any hardware
trial. The owner's offline-only restriction still applies.
