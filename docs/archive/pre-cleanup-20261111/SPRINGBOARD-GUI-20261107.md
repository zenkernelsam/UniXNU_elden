# SpringBoard GOP lock-screen milestone

The Y700 port has produced an actual SpringBoard lock-screen framebuffer with
colour wallpaper, a date and a clock. It was sampled read-only from the mapped
firmware GOP buffer after a successful system-frame transfer, sent over USB,
and reconstructed only after all rows and CRC32 matched. The owner was away;
this is a framebuffer screenshot, not an independent photograph of the panel.

Accepted candidate: `services-20261107/boot-signed.img`, SHA256
`a046134e11d065f25b10eb44cb3834ea97343502c6418097c100c1007b5ba520`.
The stock boot rollback index remains **1738713600 at footer location 0**.
Full device readback matched. A 420.125-second capture recorded no kernel panic,
no SpringBoard exception observation and zero dropped USB bytes. The UFS final
completion recheck was exercised three times and normal reads continued.
The workbench evidence is under
`D:/Desktop/Git/Patch/Y700-Elden/edl_work/services-20261107/`:

- `frames/frame-1.png`: source frame 14, RGB565 preview CRC32 `df4e0f93`.
- `frames/frame-2.png`: source frame 15, CRC32 `ff9fbe62`; a grey transition
  frame with horizontal banding. Display rendering is not fully qualified.
- `frames/report.json`, USB raw logs, and final `qualification.json` retain
  the session, pixel hashes, runtime observations and limitations.

The path uses the catalog NativeLegacyDisplay CPU presenter, the unchanged
original IOMobileGraphicsFamily ABI, actual 1904x3040 GOP memory and explicit
UI scale 2. The temporary console physical-address flag stays in a local copy.
Releasing text output retains the independent USB log ring.

Startup also needs the official skip-setup/no-auto-dim preferences in a copy of
the 512 MiB RAM Data seed. The original seed and OS/SystemOS root images remain
unchanged. The catalog AppleKeyStoreState adapter is enabled only for this
verified unprotected volatile fixture; it provides no keys or secure storage.
Normal launchd overrides disable unavailable watchdogd, locationd and nehelper
services, after real user stacks identified their blocking startup paths.

The default XNU entry starts after eight seconds. Both session-bound USB reboot
and panic recovery to fastboot after approximately 180 seconds were observed
on hardware. A bounded frame-preview channel and read-only user-stack sampler
now support unattended diagnosis. Usage is in the workbench
`research/Y700-AUTO-DEBUG-RUNBOOK-20261107.md`.

This milestone does not mean a fully usable iPadOS tablet. The home screen was
not interactively unlocked; touch, GPU acceleration, native display/backlight,
networking, audio and RTC calibration remain subsequent work. RAM Data is lost
on reboot. A UFS deadline race has a guarded final-completion recheck and host
tests, but long-term storage stability still needs qualification. Do not reuse
the RAM-only key-state policy for future persistent encrypted Data.

For subsequent peripherals, prefer board-specific Y700 evidence from ArmadaOS
Freedom and its source kernel, using mainline for shared subsystem semantics.
Neither a repository label nor a successful build substitutes for hardware
validation. Existing KShimC dual-boot exploration remains separate and retained.
