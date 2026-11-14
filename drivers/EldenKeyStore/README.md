# Elden software key operations

The v122 original21H461 `bluetoothd` first-exit stack reaches the failure path
after `readRootKeys` (`0x10062dfd4`). The existing service explicitly reports
`key-operations=off`. This establishes a missing dependency and a failing call;
it does not yet establish the exact Security.framework error.

The source is pinned to official AppleKeyStoreState
`6ff4f11d8fdf853351f8d98e1bbe1373fb8da2e6`. `NativeKeyStoreEvents.upstream.cpp`
and `TestUpstream.py` retain the original bytes. `SourcePatch.py` derives the
Elden service/glue; key wrapping, blob format and selectors follow upstream.
The upstream concurrent mutable device-key cache is replaced with per-call
derivation. Null scalar input, identity length and descriptor short writes are
checked. No key material is logged.

`elden-key-state=1` remains state-only. Explicit mode2 enables selectors10/11
(wrap/unwrap),23 (bag UUID),40 (generation), plus existing bookkeeping. The
original volatile512MiB Data, no-persistence, writable and armed-recovery
requirements remain. Identity is copied from exact8-byte nonzero
`/chosen/unique-chip-id` before service publication. **Current ADT uses fixture
identity1, not a hardware-derived unique secret.** This software format provides
no hardware data protection. Device lock/reference keys stay unsupported. The
trial does not modify Android keys and does not establish persistent pairing.

Run host checks on a system with Python3 and a C++ compiler supporting ASan/UBSan:

```sh
python3 -B drivers/EldenKeyStore/Test.py --output NEW
```

The actual generated C++ service is tested against the unchanged independent
hashlib blob/UUID oracle and Elden negative cases. IOKit classes are host doubles;
that test does not prove native registration. Output preserves generated sources,
test harness, executable and logs. Two initial host attempts failed before tests
(missing clang++, then GCC rejecting indentation in the upstream test decoder);
the harness formatting was corrected without changing the oracle/assertions.

From a fresh checkout, use the normal original-input prerequisites in
[CLEAN-BUILD](../../docs/CLEAN-BUILD.md), with:

```sh
python3 -B tools/build_clean.py --variant bluetooth --bt-exit-stack \
  --software-keystore --inputs inputs.json --output NEW --target boot --jobs 4
```

The delta builder `tools/build_elden_keystore.py --clean-build CLEAN_OUTPUT
--exit-stack EXIT_OUTPUT ...` recompiles native jobs591/592 through their closed
source overlays, preserves all other display archive members and the core
filelist, then links/prelinks and checks all30 original Apple executable regions.
These inputs are outputs of the portable source build, not historical v99 files.
The published native contract and boot qualifier bind source hashes, loaded
sections/symbols, runtime/trust, entry and mode2. The clean baseline and subsequent
delta stages have been run separately; the combined option has not yet been run
as another full empty-output build.

v123 build: kernel `3cabff1ad46f63a80a13f5b6e05453f4ad1270d618ac38f549c2c19cd2fa6cfb`;
signed boot `28f234dac478f28f7edf126d6cc17f11ed20229e07efd34750b9e01977712d09`.
Native compile logs contain no diagnostics. Original entry relative branch and
floor1738713600/footer0/parent3 remain checked. See the profile hardware record
for observations; static build success is not Bluetooth acceptance.
