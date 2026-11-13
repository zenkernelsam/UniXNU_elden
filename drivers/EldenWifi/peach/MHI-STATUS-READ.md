# Preserve MHI download-status read failures

The original BHI wait condition treats either a register-read error or a nonzero
transfer status as a reason to end the wait. It then checks the last status and
returns success when the wait returned nonzero. If a read first returns pending0
and the next read fails, the pending value survives and BHI falsely returns0.
On a first-read error, both BHI/BHIe could also inspect uninitialized status.

`MhiReadPatch.py` changes only the two firmware-download wait functions in the
exact upstream-prepared MHI `boot.c`. It initializes status, saves the read error
separately and returns that error before using transfer status. Existing PM
failure, transfer-error handling, successful completion and timeout behavior
remain; BHIe retains its original timeout precedence. No retry is added.

The actual original/repaired function bodies and pinned shim wait macro were
run with host register callbacks under ASan/UBSan. Pending-then-error reproduces
the old false success. Repaired cases cover errors on reads1–3, immediate/delayed
completion, timeout, transfer error, initial and mid-wait PM loss, and BHIe
doorbell failure. The integrated build source matches the tested derivative.
No observation establishes that this failure occurred on the tablet.

The complete125-object C runtime rebuilt and passed input/symbol/code checks.
Only `mhi_boot.o` has changed functional code.68 other objects contain a different
literal build-directory path in `__cstring`; after normalizing that exact path
and no other bytes, their loadable content matches.455 other compile inputs and
all external imports remain equal. Both `tx_status` warnings disappear, leaving
9332 compiler warnings, whose broader review remains pending.

The native WLAN provider `pwrseq_qcom_wcn_set_ol_cpr`, PCIe/DMA/firmware/network
callbacks and kernel integration remain incomplete. No boot image was built or
device accessed. See [evidence](../../../docs/evidence/mhi-status-readfix-20261113.json).

## Reproduce

`tools/build_wifi_runtime.py` applies this patch after the official MHI pre-load
hook and before compilation, preserving the original source beside the generated
file. Run [RUNTIME.md](../RUNTIME.md) with a new output directory. The earlier
unpatched runtime is reproducible at `eac20d1b76c9d809895910bfc702f218ac1e3a47`.

```sh
python3 tools/test_mhi_status_reads.py --component /path/to/runtime/component \
  --compiler clang --output /path/to/new-read-error-test
```

The tester accepts either the original prepared component or the integrated
fixed tree. For the latter it checks the saved original and derivative before
extracting the functions. The independent runtime auditor also validates
`mhi-status-read-fix.json` against the source transformation.

Final Mac output is project-local `lab/wifi-runtime-20261113/mhi-fixed`.
Windows records and object copies are in workbench
`edl_work/wifi-mhi-readfix-20261113` and `lab/records/mhi-status-readfix-20261113`.
The interrupted turn's successful initial test remains in Mac
`lab/mhi-read-fix-20261113/host`.
