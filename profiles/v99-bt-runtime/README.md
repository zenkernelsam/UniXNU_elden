# v99 Bluetooth runtime — first hardware attempt stopped at parent clock admission

2026-11-13: the owner resumed unattended testing. This exact image prepared
its runtime files/mount successfully, then refused WCN survey with−19 before
power acquisition or UART/HCI. The capture's Taycan PLL uses hardware vote-run;
the old decoder incorrectly requires manual RUN and an unused third output.
See [hardware-result.json](hardware-result.json) and
[the fixed-board analysis](../../docs/WCN-PARENT-CLOCK.md). Explicit sync/reset
and full-SHA stock Android recovery passed. Bluetooth is not yet usable.
The read-only decoder repair passes host/native component checks but has not
been integrated into this frozen candidate. The earlier build record follows.

Reproduce this exact source profile at commit `7f0704c657ca3e671007cc00034e096b54892042`.
Later shared-planner changes deliberately fail its old owned-source hashes;
do not update those hashes or silently replace this unflashed candidate.

The runtime, entry transformation and96MiB signed boot candidate now build
offline. This profile selects `elden-wcn-power=4`: it attempts real WCN power,
UART and HCI initialization after its gates. It is **not read-only** and has
had not been flashed when the following offline record was written.

Candidate in the paired workbench:
`edl_work/bt-runtime-v99-20261112/candidate/boot-signed.img`

- Image SHA256: `d385e428b0753c13a09c2befd479a98a8d5ead287545604139164aec214f8215`
- Session: `3d74d3ccaed21f53`
- Entry-transformed kernel: `75fe6d69ccc0bc00f264573cd6f60a489db12e228bc3c23b73c18355c1b1c80f`
- Floor1738713600/footer0, parent location3, original FD/loader/Data contracts,
  56MiB payload, eight-second default boot and bounded recovery are retained.

## Minimal runtime installation

The original21H461 BlueTool job already has the required Mach service and
`/usr/sbin/BlueTool -R` path. This helper only reads a public read-only sysctl,
so this candidate retains the original mobile/Adaptive/pressured-exit job.
Its lifecycle with the replacement helper remains a hardware validation item.
It does **not** install the root/non-pressured-exit job template emitted in v98.

All561 original jobs and the1,640,626-byte launchd cache remain untouched.
There is no duplicate bluetoothd executable or rewritten signed cache in the
payload. Only the87,488-byte owned arm64/iOS17 BlueTool is embedded.

Before launchd, a mode4-only stage creates that exact file on the qualified
fresh512MiB RAM Data, verifies bytes/owner/mode, and uses the existing read-only
bindfs union over `/usr/sbin`. The sealed lower bluetoothd stays visible.
Failures roll back the mount; WCN mode4 refuses to start unless staging passed.
Prepared files are not by themselves proof that the service has executed.

The original trust-cache parts are byte-preserved; one part adds BlueTool's
CDHash `e88b9aee568d043baf7a20caf4696c051b2ca802`. The new container SHA is
`a6758681cd8ce0f364411bf65fc1207514b3470b0c16699e2c1a0d12f22a2c65`.

## Transport selection, without a false bootstrap success

The original F3 branch at file offset0xddd20 chooses Skywalk for the standard
UART control name. This candidate uses its existing ordinary stream path.
One ADT fixup changes `a0050034` to `1f2003d5` after page signature validation.
The F2/BlueTool call remains intact, so success still requires the real HCI and
host/address checks supplied by v98.

`TextGuard.h` additionally requires this exact four-byte rule, ordinary ARM64,
subtype0, thin-image offset0 and CodeDirectory hash
`fe7bc182386a2766c7574f8b48412ff9df113564`. Ambiguous covering signatures refuse
the fixup. Other pre-existing fixups are preserved, including the audio guard.
The stage also requires the exact combined ADT table before mounting.

The code uses `ubc_get_cs_blobs()` under the existing page-fault
`paging_in_progress` lifetime, as documented by XNU's own validator. It adds no
vnode lock to that path. The original OS files are never rewritten.

## Validation and entry relocation

The two changed core C units (`kern_exec`, `ubc_subr`) were compiled scalar;
the20 Bluetooth objects and two existing native hooks retain their own checks.
Native/prelink passes,30 original Apple executable ranges remain unchanged,
and each firmware/helper payload is present once. Rule/identity rejection and
menu/ADT preservation host tests pass. Complete FAT, assets, trust, entry,
session and AVB audits pass; the updated auditor also rechecks the old v96 image.

The core layout moved `__start` and `_start_first_cpu` by0x64000. The first boot
audit refused the old fixed return address. `audit_entry_relocation.py` then
confirmed both links use the identical start.s object, whose executable/data
sections match the committed v91 contract. Its entry B word and0x1bd4 relative
target are unchanged. The100-byte blue-entry body remains identical; only its
final return branch uses the qualified new symbol address0xfffffff0074ce05c.
Consequently `entry_branch_target_equal=false` (literal old address) and
`entry_branch_target_qualified=true` are both intentional. See `entry-layout.json`.

This is static/host evidence. Actual staging, ordinary-stream HCI initialization,
chipset/optional-command behavior, the original job lifecycle, pairing and
power/sleep remain untested. Do not use a static pass as authority to bypass
fresh identity, battery, full-write SHA or recovery checks.

## Reproduction

Use `build_elden_bt_runtime.py` with the qualified v98 helper, original21H461
cache/job and v94 trust container. Build the kernel with
`build_wcn_kernel.py --profile v99-bt-runtime --runtime INPUT` and the same
explicit parents/dependencies as v98. Run `audit_wcn_kernel.py` with the same
profile/runtime. Then run `audit_entry_relocation.py` against the v94 baseline,
new build and verified core output, saving `entry-layout.json` in the build.

After `prepare_v91_entry.py`, use `build_v91_boot.py` and `audit_v91_boot.py`
with `--wcn-report BUILD/result.json --bt-runtime INPUT --entry-report ENTRY/result.json`.
Do not combine those options with the older `--fbwc-report`/`--battery-ui` options.
The Bluetooth menu retains battery UI, physical touch and power/thermal guards;
it removes only the software Settings replay and UIKit trace and fits1013 bytes.

The runtime input, core delta, entry layout and signed candidate records are
under `edl_work/bt-runtime-v99-20261112/`; source/read-only OS evidence is under
`lab/records/bt-runtime-20261112/`. All work was offline. Old candidates are
preserved; the full peripheral Goal is not complete.

The inherited `plan.json` field `runtime_enabled=false` describes the kernel's
default without boot arguments. It does not describe this packaged menu: the
explicit mode4 above enables the runtime attempt. Likewise the kernel-stage
report's `boot_packaged=false` predates the separate boot-stage report.

For the new runtime step, run from this source checkout on macOS (Python3.12):

```sh
python3 tools/build_elden_bt_runtime.py \
  --helper /path/to/qualified-v98/BlueTool \
  --launchd-cache /path/to/original21H461/launchd.plist \
  --bluetool-job /path/to/original21H461/com.apple.BlueTool.plist \
  --trust-cache /path/to/verified-v94/kernel.macho.trustcache \
  --output /path/to/new-v99-runtime
```

The original cache is `/System/Library/xpc/launchd.plist`; the original job is
`/System/Library/LaunchDaemons/com.apple.BlueTool.plist` in the matching SystemOS.
Both inputs are hash checked. Obtain the helper from its owned-source builder
in v98; do not substitute the v98 generated job for the original Apple job.
Use a new output directory for every build/audit. The full WCN parent arguments
are listed in [v97's build recipe](../v97-wcn/README.md#reproduce); add the v99
profile/runtime options stated above. The entry step takes this build's
`kernel.json` as `--plan`. For FD, loader, Data, signing and parent-vbmeta
arguments use the [boot recipe](../v91/boot-build/README.md) and its
[source-built asset recipe](../v91/boot-build/ASSET-REBUILD.md).
