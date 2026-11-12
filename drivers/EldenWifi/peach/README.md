# Peach backport onto the official Wi-Fi source base

The TB322FC delta is now prepared for the official Ath12kWifi Linux7.0.14 base.
Six changed C units compile as scalar ARM64/iOS17 objects. This is **not** a
complete125-object stack build, native WLAN gateway, kernel image or hardware
qualification. The owner's offline-only restriction remains in force.

`sources.json` pins Linux458c6079 and the complete nine-driver-file plus power
header delta from h0cheung's `dc7d14dc64c8d4f86220b30cd126b17d805c8474`.
`upstream-peach.patch` preserves that original delta and its source licenses.
`prepare_elden_peach.py` reads Git blobs, applies unique exact contexts and writes
a new snapshot. It never patches either reference checkout.

The version adaptations are explicit:

- Insert Peach's enum/HAL/hardware entries at the7.0 array boundaries; that
  version has no later IPQ5424 entry used by the original patch's context.
- The original512/128/128 client limits already exist in7.0's DEFAULT memory
  profile, which Peach selects. Do not introduce7.2's incompatible `.client`
  structure. The absent host-owned ML-ID feature is not selected.
- Apply the new coherent DMA-mask setup only to Qualcomm17cb:110e. Other
  endpoints retain their previous mask behavior.
- Apply the new AUX firmware-capability check to the Peach/TME profile;
  existing non-Peach AUX behavior remains, including absent capability bits.

The independent Peach hardware profile retains external-clock selection via
`internal_sleep_clock=false`, TME-Lite, board API1, the selected QMI feature
bits and the hardware-major2 check. These are reference configuration values,
not device observations.

## Failure handling

The actual prepared QMI bodies were exercised with host allocation/transport
fixtures. Tests cover OL-CPR bounds/copy/freeing, failure ordering, send
cancellation, non-Peach AUX behavior and TME result handling. They do not test
TLV encoding, the actual target-capability transport or hardware execution.

Correction to the earlier broad summary: TME failures are not all fatal. The
reference has one explicit nonfatal response, hardware-restricted `0x53`, after
the TME request was sent. It logs that restriction and continues; it does not
log a successful TME load. Other firmware and transport errors stop the path.
This preserves the reference's policy, not independent proof of the comment's
claim about stock Android behavior. Local device confirmation is still pending.

OL-CPR provider failure remains fatal. Compilation selects
`CONFIG_POWER_SEQUENCING_QCOM_WCN=1` and verifies that
`pwrseq_qcom_wcn_set_ol_cpr` is a real unresolved dependency. The header's
unsupported fallback is not used to hide a missing implementation. A valid
native WLAN owner/provider must be supplied before kernel integration; the
BT-only acquire path is not that provider.

## Source and compiler evidence

The initial Windows Git archive converted some LF source into CRLF. It is
superseded as a portable input. `export_wifi_linux.py` disables automatic EOL
conversion and verifies all6703 exported regular files/symlinks against Git
blob identities; `linux-input.json` pins the corrected archive. Earlier input
and compile attempts remain preserved outside `research`.

The final compile-input audit verifies171 consumed files:10 backport inputs,
75 component files,67 Linux inputs/copies,2 generated headers and17 compiler
builtin headers. The latter are hashed and recorded, not matched to a Git
source. Unused case-colliding netfilter headers do not enter these six units.

Compilation is not warning-free:613 warnings were retained. Source-line
comparison assigns294 to unchanged headers,294 to unchanged driver lines and25
to changed lines. The changed cases are16 debug-mask conversions,2 PCI wildcard
conversions,2 Linux error-pointer conversions,2 bounded QMI send results and3
firmware lengths with preceding U32 size guards. This provenance review is not
proof that every inherited warning is harmless. Full-stack/ABI review remains.

See [the consolidated evidence](../../../docs/evidence/peach-backport-20261112.json).
Windows records are `lab/records/peach-backport-v2-20261112`,
`peach-linux-lf-20261112` and `peach-native-20261112` in the workbench.
Mac final outputs are project-local `lab/peach-backport-20261112/compile-lf` and
`include-audit-lf`. Only trace stubs and pinned headers are prepared here; the
complete upstream OS/runtime transformations and remaining stack units still
need integration and recompilation before linking.

## Reproduce

Python3.12, from this source checkout; every output path must be new:

```sh
python3 tools/prepare_elden_peach.py --linux-repository /path/to/linux-ath12k-7.0.14 --output /path/to/prepared
python3 tools/export_wifi_linux.py --repository /path/to/linux-ath12k-7.0.14 --output /path/to/linux-export
python3 tools/compile_peach_sources.py --component-repository /path/to/Ath12kWifi \
  --linux-archive /path/to/linux-export/linux.tar.gz --backport /path/to/prepared \
  --llvm /path/to/llvm/bin --output /path/to/native
python3 tools/audit_peach_compile_inputs.py --build /path/to/native \
  --component-repository /path/to/Ath12kWifi --linux-archive /path/to/linux-export/linux.tar.gz \
  --backport /path/to/prepared --output /path/to/input-audit
python3 tools/test_peach_flow.py --backport /path/to/prepared --output /path/to/host-flow
```

Compilation requires macOS/Xcode. Windows host flow tests can add
`--wsl-distro Ubuntu-22.04 --compiler gcc`, using Windows TEMP for output.
No tool accesses or flashes the tablet. Existing v100/v99 candidates remain
unchanged, and the complete peripheral Goal is not achieved.
