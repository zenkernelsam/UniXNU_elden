# Peach OL-CPR planning — no hardware I/O

The TB322FC Linux change
[`dc7d14dc64c8d4f86220b30cd126b17d805c8474`](https://github.com/h0cheung/tb322fc-linux-kernel/commit/dc7d14dc64c8d4f86220b30cd126b17d805c8474)
does more than add PCI110e. Its QMI target-capability handler requires successful
firmware-directed PDC configuration before TME-Lite; TME-Lite failure and later
QDSS configuration failure stop firmware bring-up. There is no valid port that
replaces those results with an unconditional success.

`OlCprPlan.c` implements only the computation used by that power-sequencing
driver, with the ten PMU aliases and four PDC targets from the fixed TB322FC DTS.
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

This component does not send QMP/RPMh, touch registers, set a voltage or claim an
applied vote. Runtime integration still needs the actual QMI reply, shared WCN
ownership, board limit/current-state checks, command acknowledgment and failure
handling. The existing Bluetooth/charging candidates do not include this code.

Host ASan/UBSan checks compare20,128 inputs against the exact extracted Linux
arithmetic body and cover all aliases, maximum aggregation, invalid/unknown
names and failure preservation. The ARM64/iOS17 object is scalar with no imports.
See [evidence](../../docs/evidence/wifi-olcpr-20261112.json). Neither compilation
nor these synthetic inputs qualify a real firmware voltage request.

```sh
python3 tools/test_olcpr_plan.py --linux-commit-json /path/to/pinned-commit.json \
  --board-dts /path/to/verified/sm8750-lenovo-elden.dts --output /path/to/new-host
xcrun --sdk iphoneos clang -target arm64-apple-ios17.0 -O2 -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin -mgeneral-regs-only \
  -c drivers/EldenWifi/OlCprPlan.c -o /path/to/new/OlCprPlan.o
```

For Windows host tests add `--wsl-distro Ubuntu-22.04 --compiler gcc` and use
Windows TEMP for the new output. The test validates the Linux arithmetic hash
and exact DTS hash before compiling the oracle. Source evidence is in the paired
workbench `lab/records/wifi-peach-backport-20261112`; results are in
`lab/records/wifi-olcpr-20261112`. Mac object/results are project-local
`lab/olcpr-20261112`. QMI/backport integration, executor, PCIe/MHI and networking
are still pending; no new boot image was built or device accessed.
