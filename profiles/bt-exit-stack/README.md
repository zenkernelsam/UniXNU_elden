# Bluetooth first-exit stack (v122)

`tools/build_clean.py --variant bluetooth --bt-exit-stack ...` builds the current
source baseline plus one bounded diagnostic in the already name/CDHash-qualified
exit observer. It reads only the current thread's EL0 saved state and up to8
monotonically increasing, aligned user frames. Failed copyin stops the trace.
No exit status, authentication or launch constraint is changed.

The actual observer host test covers wrong context, alignment, user bounds,
copy failure, cycles and the8-frame cap. Native code is scalar; all30 original
Apple execution regions and the existing startup branch contract are verified.

Hardware record: [hardware-result.json](hardware-result.json). The stack base
`0x1010da000` is inferred by matching multiple original daemon call sites, not
from a measured VM map. Return `0x100632614` lands after the fatal branch following
`readRootKeys` at `0x10062dfd4`; the failing return is tested at its caller. The
exact keychain error has not been captured. Current service reports key operations
off. Wi-Fi Off/unavailable and Bluetooth No Available were reported by the owner;
neither peripheral is accepted. Sync return/full stock restore passed.
