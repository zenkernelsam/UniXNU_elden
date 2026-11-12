# Offline paired-store candidate — not device tested

Inspection of v94's actual ARM64 code disproved the proposed byte-load issue:
the existing source reads already use64-bit LDP. Its hot loop still uses four
individual64-bit stores per32 bytes. This candidate tests paired general-register
STP stores, only after the established Normal-NC mapping qualification.

`CopyPairs.h` retains the existing layout checks, requires16-byte aligned bases
and strides, and declines overlapping virtual ranges. Unsupported cases use
the unchanged copier; invalid bounds return without writing. Visible rows and
tails are copied exactly, leaving padding unchanged. The existing final DSB and
retirement stay in place. Two path-selection messages are outside the copy timer.

469 padded rectangles passed ASan/UBSan comparison tests, including tails,
unaligned eligibility, bounds, overlap and execution of the original fallback.
The ARM64 object contains actual paired data stores and no FP/SIMD. The original
30 Apple executable regions and unchanged build inputs are checked as before.
These checks establish an offline test candidate, not throughput or UI success.

Build with `tools/build_fbwc_kernel.py --profile v95-fbwc-pairs` and the same
verified v91 inputs. It inherits v94's observation limits and v93's mapping
implementation. Pass the resulting report to the boot packer and static auditor.
The owner requested offline work; this version must not be flashed until that
constraint changes. v94 remains the hardware-tested framebuffer baseline.
