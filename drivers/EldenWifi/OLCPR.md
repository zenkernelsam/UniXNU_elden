# Peach OL-CPR planning — no hardware I/O

The TB322FC Linux change
[`dc7d14dc64c8d4f86220b30cd126b17d805c8474`](https://github.com/h0cheung/tb322fc-linux-kernel/commit/dc7d14dc64c8d4f86220b30cd126b17d805c8474)
does more than add PCI110e. Its QMI target-capability handler requires successful
firmware-directed PDC configuration before TME-Lite; TME-Lite failure and later
QDSS configuration failure stop firmware bring-up. There is no valid port that
replaces those results with an unconditional success.

The existing `EldenBluetooth/WcnPower.c` already contained this planner and its
BT-owner-scoped sender. The initial Wi-Fi implementation duplicated the math;
it is now a thin adapter to that shared provider. `OlCprPlan.c` accepts decoded
PMU fields, not serialized QMI TLV bytes. The shared core owns the ten aliases,
rounding, maxima and four PDC target names from the fixed TB322FC DTS.
Source lineage is `drivers/power/sequencing/pwrseq-qcom-wcn.c`, GPL-2.0-only,
Copyright (C)2024 Linaro Ltd.; the board mapping is from
`6f97250ead155fc3b980856323e99a4623dae73d`.

- Wake/sleep values use the original integer rule: round up to4, subtract16,
  add30/10. Values remain in the firmware/QMP convention; no new unit conversion.
- Aliases sharing a rail take the maximum separately for wake and sleep.
  Order follows the DTS: s5f/rf, s4d/bb, s3g/rf, s7i/rf.
- At most16 input entries and eight planned commands. Empty names, overflow
  and all-unknown voltage requests fail. Unknown entries mixed with known ones
  are counted/skipped as in Linux; no-voltage entries do not create votes.
- Complete validation precedes output publication; errors leave the caller's
  plan unchanged. A successful empty plan means no voltage was requested.

The Wi-Fi adapter does not send QMP/RPMh, touch registers, set a voltage or claim
an applied vote. The existing WcnPower sender retains its Ready/epoch/quarantine
checks. Its native Context is still BT-worker owned: there is no Wi-Fi QMI
gateway, and BT acquire must not substitute for WLAN power-on. Runtime work
still needs the actual QMI reply, WLAN ownership and acknowledged execution.

Host ASan/UBSan checks compare20,128 inputs against the exact extracted Linux
arithmetic body and cover all aliases, maximum aggregation, invalid/unknown
names and failure preservation. After consolidation,4096 legacy/new comparisons,
all eight command-failure positions and the original12 power scenarios/321
assertions also pass. Old public APIs, error codes, command bytes and quarantine
behavior are preserved. Both ARM64/iOS17 objects are scalar; the adapter imports
four shared helpers, with bzero/strcmp left for the existing kernel.
See [current evidence](../../docs/evidence/wcn-cpr-shared-20261112.json); the
[initial standalone evidence](../../docs/evidence/wifi-olcpr-20261112.json) remains historical.
Neither compilation nor synthetic inputs qualify a real firmware voltage request.

```sh
python3 tools/test_olcpr_plan.py --linux-commit-json /path/to/pinned-commit.json \
  --board-dts /path/to/verified/sm8750-lenovo-elden.dts --output /path/to/new-host
python3 tools/test_wcn_cpr_compatibility.py --output /path/to/new-compatibility
python3 tools/build_shared_wcn_cpr.py --llvm /path/to/llvm/bin --output /path/to/new-native
```

For Windows host tests add `--wsl-distro Ubuntu-22.04 --compiler gcc` and use
Windows TEMP for the new output. The test validates the Linux arithmetic hash
and exact DTS hash before compiling the oracle. Source evidence is in the paired
workbench `lab/records/wifi-peach-backport-20261112`; results are in
`lab/records/wifi-olcpr-20261112`. New shared results are in
`lab/records/wcn-cpr-shared-20261112`; Mac final objects are project-local
`lab/wcn-cpr-shared-20261112/native2`. Link `libEldenWcnPower.a` once plus
`OlCprPlan.o`; omit a second WcnPower.o from Bluetooth/Wi-Fi archives. The frozen
v99 image retains its original implementation and its recorded source commit.
QMI/backport integration, native WLAN ownership, PCIe/MHI and networking remain
pending; no new kernel/image was linked or device accessed.
