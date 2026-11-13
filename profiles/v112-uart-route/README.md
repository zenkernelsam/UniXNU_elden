# WITHDRAWN v112 GIC address error

Do not build/flash this historical candidate. UartBackend.cpp used
0x01600000 for GICD; the real same-board DTS address is0x16000000. The
successor v113 binds it to a named header and checks against the board DTS.
The shared boot qualifier now rejects v112. These sources/hashes remain
only to explain the failed run and prevent accidental reuse.

Image98a860f7…/session1b2fd09da953f03a passed the then-existing static
checks, which missed that address defect. Capture stopped at31,743 bytes,
before WCN prepare/UART-acquire report. No panic text or sync-reset marker
was captured. Device returned to fastboot at221.8s; reset cause is unproved.
Stock restoration and Android/root/green recovery passed. Do not interpret
the absence of a route-write report as proof of which instruction failed.

This attempted Linux FIFO route0xf plus a masked-GIC check; its hardware
result is invalid for judging the route itself. v113 records the correction.
