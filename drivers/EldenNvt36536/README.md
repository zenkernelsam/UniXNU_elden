# Elden NT36536 point protocol

2026-11-09: the original-chip identity test reached the SPI engine but refused
FIFO-disabled mode before any touch transaction. Power resources were returned
and the device reached fastboot. Mode4 now collects the inherited GENI and SMMU
global routing state for wrapper SID0xa3 and GPI SID0xb6, with no SPI/DMA commands
or SMMU programming. The v24 survey candidate is built and signed, untested.
See `docs/TOUCH-DMA-SURVEY-20261109.md` in the device repository. Physical ID,
firmware upload and real touch remain unqualified on the tablet.

This board module decodes the native point reports used by the TB322FC NT36536
firmware. Protocol, platform and runtime sources now supply the separately
gated SPI/firmware/HID paths; those additions have host build evidence only.
Physical input remains unqualified until the transport and firmware profile
have passed hardware checks.

Protocol facts come from `h0cheung/tb322fc-linux-kernel` at
`6f97250ead155fc3b980856323e99a4623dae73d`,
`drivers/input/touchscreen/novatek-nvt36xxx-spi.c`. The audited source hash is
`67295bfd775d8eafdd09e07db9d70c690054dec72dcae0c922345944dd1b1a66`.
The reference stays in the workbench `research/nt36536-elden-20261107/`.

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
