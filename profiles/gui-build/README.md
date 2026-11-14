# Source-buildable 8GiB GUI profile

The complete build_clean.py --variant gui invocation has also completed from an
empty output through final signed-boot audit. A separate deny-old-paths test found
Clang's early assembly input check; the builder now stages start.s as a real new
file while preserving its logical name. The corrected assembly and the other
nine units pass expected section/symbol checks with old paths inaccessible.

Ten changes over the source-rebuildable v91 base reproduce the actual v109
GUI branch: display mapping/cache and paired stores, reliable HID delivery,
IOKit observation, RT-memory treatment and the coherent8GiB bootstrap pool.
Original21H461 IONetworkingFamily is added during prelink, giving31 preserved
Apple execution ranges. No Bluetooth experiment is implicitly merged.

Every unit is rebuilt from source providers using a closed VFS. No previous
v100/v104/v106/v109 kernel or object is needed. The raw source origins are
provenance only; the declared providers name repository/source/dependency inputs.
The arm64 startup assembly is rebuilt and compared against its actual section
and symbol contract along with the C/C++ units.

Validation reused the base objects freshly compiled in the same isolated run,
compiled all10 GUI changes, linked the exact native loaded-content contract,
prelinked the original31 code ranges, generated a fresh entry/ADT/FAT and signed
the boot. Final audit confirms8GiB and1738713600/footer0/parent3. This new generated
image has not been flashed. Use build_clean.py --variant gui as documented in
docs/CLEAN-BUILD.md.
