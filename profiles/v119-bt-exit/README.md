# v119 native Bluetooth exit reason

This keeps v118's exact page rules and real controller path. A bounded diagnostic
in the already-wired exit observer selects only process name bluetoothd with
original CDHashfe7bc182386a2766c7574f8b48412ff9df113564, at most16 times.
It exposes the real raw wait status and existing os_reason; it changes no exit,
launch, signature, sandbox or HCI decision. The legacy ELDEN_ICON_EXIT label is
retained, so correlate by PID and the exact observer predicate rather than name
alone. Other existing IconServices exit lines remain independent.

Build with the v118 recipe, profile v119-bt-exit and baselineV118. Its immutable
core source provenance remains V110. Two core objects are regenerated from
those inputs; only the observer include changes compared with v118. The audit
checks its complete derived include, source hashes, all retained compiler/link
inputs, SPMI consumers, original30 Apple code regions and entry contract.
The complete v119 signed image again retains1738713600/footer0/parent3.

Hardware: the first bluetoothd PID79 exits with raw wait status0x100 (exit1),
os_reason namespace0/code0/text none. No recorded data fault; later launches
are rejected with category c[8]p[1]m[5]e[5]. HCI initialization succeeds again
but no native client connects. This distinguishes the initial startup failure
from the subsequent launch refusal; it does not identify the exact failed
startup call. Known native errx(1) paths include listener registration and
daemon startup, so next read their actual errors rather than suppressing checks.

Typed sync/reset, physical fastboot and full stock SHA restore all pass. Device
returned to Android/root/green/boot-completed and was asked to sleep. Raw capture,
drop and exact image/session are in hardware-result.json. No scan/pairing claim.
