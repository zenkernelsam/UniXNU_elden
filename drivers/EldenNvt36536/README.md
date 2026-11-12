# Elden NT36536 point protocol

Current evidence (2026-11-12): v94 identifies the controller, reads7579 point
frames and stops with invalid=0. Software input through the shared HID path
opens Settings; that run has no physical contact changes. Real-finger gestures
remain unqualified. See [input status](../../docs/INPUT-STATUS.md) for the
source-bound audit, known error-path risks and the next functional trial.

This board module decodes the native point reports used by the TB322FC NT36536
firmware. Protocol, platform and runtime sources now supply the separately
gated SPI/firmware/HID paths. Transport evidence does not replace an end-to-end
physical input check. Keep tested frozen profile sources for candidate builds.

Protocol facts come from `h0cheung/tb322fc-linux-kernel` at
`6f97250ead155fc3b980856323e99a4623dae73d`,
`drivers/input/touchscreen/novatek-nvt36xxx-spi.c`. The audited source hash is
`67295bfd775d8eafdd09e07db9d70c690054dec72dcae0c922345944dd1b1a66`.
Read the file from the workbench `research/tb322fc-linux-kernel` Git object at
that commit (the checkout may be sparse). Historical extracts are in
`lab/records/nt36536-elden-20261107/`, outside the reference-clone directory.

Reports contain 108 bytes after the register byte, a two's-complement checksum
over bytes 1..65, ten six-byte finger records and big-endian 16-bit positions.
This differs from the sheng NT36532E THP matrix algorithm. Inactive identifiers
are omitted; a valid empty frame means release all. Invalid/truncated/duplicate
or out-of-range active reports leave the output unchanged. The transport must
also enforce a stale-report timeout and release contacts during shutdown.

The caller supplies inclusive coordinate maxima from the verified firmware
profile (Y700 source candidate: 19040 and 30400). Coordinates become HID's
0..32767 exactly once. Width is reported; pressure is not invented from the
controller's binary touch status. This first decoder supports finger reports,
not unverified pen features.
