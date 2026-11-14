# Native 21H461 Bluetooth routing component

Later integration: profiles/v118-native17 applies these exact rules to original
signature-verified pages, including the separately retained F3 rule, rather than
installing the signed component below. v118/v119 real HCI stays ready and model-null
fault is no longer recorded, but native client remains absent. v119 captures initial
exit1 with no os_reason, followed by launch-constraint failures. Native daemon
initialization, supported capabilities and scanning/pairing remain incomplete.

The user correctly recalled that sheng ran iPadOS17 before27. The complete
published sheng history confirms21H461 at `1cab151` (2026-10-25); `e9865b0`
(2026-10-28) selects27/24A446, following a26 profile. Bluetooth's first recorded
work (`aa79546`, 2026-11-05) explicitly targets27, and its device acceptance
(`2d4b58b`, 2026-11-06) explicitly names27.0.1. QcaBluetooth's full history
startsNov5; native routing first appears in `8d36583` as an iPadOS27 adapter.
No native17 Bluetooth implementation was found in those fetched refs. This
does not establish the absence of unpublished or removed work.

Use sheng's older17 code for features it actually supported, including display,
input and Wi-Fi. For Bluetooth, follow its verified standard-HCI methodology
while deriving addresses and ABI from our exact original17 executable.

Reference: [QcaBluetooth native adapter](https://github.com/UniXNU/QcaBluetooth/blob/22ff97f93416452335bb61badb14c762da653487/tools/native_ios27.py),
whose file license is BSD-2-Clause-Patent. No27 address or arm64e pointer is used
in Native17.py. This module implements version-locked routing and instruction
emission; the original Apple executable remains an external input.

## Original binary and failure

- iPadOS17.7.11/21H461, ordinary arm64/subtype0, UUID
  `0f836381-d07b-36e4-9dbb-8a44782b9c14`.
- SHA256 `3f1e4493ea6a76976625f7af58a8f330c0c90d82d9020b11110b4f3f5b758df4`.
- v117 hardware:12 faults at file offset0x2f2fc0/FAR0x2a8 before H4 connection.
- `0x1002f2fbc` calls getter0x10004681c; the next instruction reads
  `[x0,#0x2a8]`. Getter loads the model at0x10093e6e8.
- Format6 chained rebase in dispatch_once block0x1008deea0 points to factory
  0x10031ad30. After73 failed product queries and a false VM check, the original
  branch0x10031cc90 returns without constructing a model.
- Factory calls thunk0x1002d50e4; constructor0x1002d4680 eventually stores its
  actual type via0x1002d50b4. The native naming table calls type0 THIRD_PARTY_DONGLE.

## Implemented scope

Native17.py binds the whole original SHA and UUID, then uses verified unused
executable padding0x2c00..0x3400 for branch thunks. It routes all factory
constructors to type0, permits the unknown-product fallback to construct the
real object, and makes only the daemon's VM getter false. It accepts type0 in
registration while preserving the original1..3999 range and all invalid cases.
Type0 clears the0x878-byte vendor table, four vendor hooks and vendor opcodes;
both callback functions retain their generic callback store and skip vendor
registration. Existing nonzero types keep the original branch behavior.

Original BlueTool/F2 and F3 stream selection bytes remain untouched. No HCI
reply or capability is fabricated. Every edit is reported and the full image
is reconstructed from original plus those edits to detect hidden changes.

## Validation and reproduction

```text
py -3.12 -B tools/test_bt_native17.py ORIGINAL_BLUETOOTHD --output NEW_OUTPUT
```

Requires Unicorn2.1.4.124 original/patched instruction cases pass at two ASLR
slides: registration bounds, both callback paths, stale vendor-state clearing,
nonzero behavior and the complete unknown-product factory's branch traversal.
External product queries, allocation, logging and the heavy constructor body
are modeled. This does not test iOS services, actual constructor dependencies,
HCI compatibility, scanning or pairing. Initial harness failures were a too-small
LINKEDIT memory mapping and treating an intentional adjacent vendor hook as a
canary; both corrected without changing adapter behavior or original assertions
about returned registration bounds and generic callback preservation.

On macOS:

```text
python -B tools/build_bt_native17.py ORIGINAL_BLUETOOTHD --output NEW_OUTPUT
```

The signed routing-only component is9893760B, SHA256
`9c2888d96a7e301472f12f0841c4044450c4c7273e48dbca4dc105181539b15c`,
CDHash `71ff504eb4a29c2812bf9400af9831dfbdb82263`.127 original entitlements and
the com.apple.bluetoothd identifier remain equal; strict signature verification
and pre-LINKEDIT byte comparison pass. Signing only alters permitted signature
load-command fields outside the reported routing changes.

**Not installed, no boot candidate or hardware acceptance.** v117's signature
guard intentionally rejects this new daemon; do not drop it into that payload.
Next integration must preserve whole-image identity and the existing F3 stream
rule. Capabilities must subsequently follow real supported-command/LE-feature
responses, optional command failures must retain correct semantics, and H4
close/key handling remain to be validated. The retained original GUIv109 and
stock recovery inputs stay frozen.
