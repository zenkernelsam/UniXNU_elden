# v120 diagnostic XPC markers

Exact21H461 userland thunks preserve native XPC result/errx arguments, LR/SP and
callee-saved registers. Direct syscall179 emits an identity-gated marker; twelve
instruction tests at two slides and4987 generated-rule/identity checks pass.
Adjacent edits are packed into31 rules, within the existing32 limit. Two core
objects and all static image/entry/SPMI/Apple/AVB checks passed.

Hardware still completes real HCI but emits no XPC markers before exit1. This
does not prove the instrumented calls were never reached; the marker path is
not yet validated on this exact daemon. v121 returned to v119 and observed
stderr inside the kernel. All trials restored stock Android by typed return and
full SHA. No launch/sandbox policy changed and no pairing is claimed.

Reproduce runtime with prepare_bt_native17_runtime.py --trace-xpc and the original
input; use build_bt_native17_kernel.py --profile v120-bt-xpc, baselineV119.
These are incremental diagnostic recipes, not yet a clean-clone entrypoint.
See hardware-result.json for capture/drop/image/session.
