# v91 native link and prelink

This profile links the newly built 1,010 XNU objects and 14 project archives.
It regenerates the binary symbol-set metadata from ordered JSON and verifies its
original hash. The compiler runtime is supplied by Xcode and hash-checked.
`SDKROOT` is cleared for the linker so an ambient macOS SDK cannot alter the
recorded iOS 17 SDK-version field.

```sh
python3 tools/link_v91_kernel.py \
  --xnu-objects /path/to/xnu-object-output/build \
  --native-archives /path/to/native-output/archives \
  --prelink-repository /path/to/XnuPrelink-git \
  --original-kernelcache /path/to/21H461/kernelcache.decompressed \
  --output /path/to/new-image-output
```

The pinned prelink tool is materialized from its Git commit into the new output;
uncommitted changes in the supplied checkout are not used. The original kernelcache
must match the hash in `plan.json`. The resulting kernel is **not a signed boot
image** and does not yet include the separately qualified entry/AMFI transform.

The portable build passed: all 50 native loadable sections, their addresses and
attributes, global symbols and immutable load commands matched v91. The final
prelinked image retains the exact executable bytes of all 30 original Apple
components. Artifact UUID/debug-link metadata may differ; the observed prelink
information difference was `_PrelinkKCID`. Full boot packaging and device
qualification remain separate steps.
