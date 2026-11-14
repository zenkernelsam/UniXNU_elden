# v118 exact21H461 native routing trial

v117's actual WCN7861 HCI initialization is unchanged. v118 adds the version-bound
Native17 model/type0 routing through the existing post-signature page-fixup path.
It uses the original sealed bluetoothd and exact original CDHash; the separately
signed routing-only component is not installed. Original561 launchd jobs, BlueTool,
trust cache, loader, Data seed and recovery/menu are unchanged except session.

`prepare_bt_native17_runtime.py` emits18 Bluetooth rules (including the existing
F3),22 total ADT rules within the existing32-rule capacity. Each spans at most16
bytes and stays within one4K page. The full exact rule/identity matrix passes3077
ASan/UBSan checks; Native17's124 original/patched instruction cases pass separately.
The full rule table and its864B binary guard occur uniquely in the final kernel.
Only kern_exec.o/ubc_subr.o are rebuilt; signature coverage now checks the entire
rule length. All other core objects/archives, real SPMI consumers, original30 Apple
code ranges, start.s/entry, FAT mirrors and AVB boundaries pass independent checks.

```text
py -3.12 -B tools/prepare_bt_native17_runtime.py --original ORIGINAL17 --runtime V99_INPUT --output INPUT
py -3.12 -B tools/test_bt_native17_guard.py --runtime INPUT --output NEW_TEST
```

On macOS use `build_bt_native17_kernel.py --baseline V117 --core-build V110
--compile-cwd CAPTURED_BSD_DEVELOPMENT --runtime INPUT --prelink-repository PRELINK
--original-kernelcache ORIGINAL --llvm LLVM --output NEW_BUILD`. The compile cwd
must contain the captured BSD generated headers; a first wrong-cwd compiler crash
is preserved in lab/native17-v118-20261114/build. Final output is build2. Then run
`audit_spmi_gate_kernel.py --profile v118-native17` with baselineV117/componentsGATE/
coreV91 and the standard prepare_v91_entry/build_v91_boot/audit_v91_boot recipe,
passing `--wcn-report` and `--bt-runtime`.

Hardware image3ae13d96344097aa1cbfa2ba3031e124db58de0ae2c6aa7a83ca2680006c6d83,
sessionda34670a4d478362: real HCI ready again, patches applied; old model-null data
fault was not recorded. No native client connected. Eleven subsequent launches
were rejected with c[8]p[1]m[5]e[5] (in-tc-with-constraint-category). The first
process's exit reason is missing; do not assume launch constraints explain the
first exit or globally disable them. Next capture that reason before changing
policy or runtime packaging. Capture1168878B/drop4; typed sync/reset, actual
fastboot and full stock SHA restore passed. Android/root/green and sleep request
observed. No pairing, scanning, repeatability or full userland success claim.
