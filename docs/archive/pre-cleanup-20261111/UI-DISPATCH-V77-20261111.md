# UIKit input-delivery candidate v77

This is an offline-prepared candidate, not a hardware result. The paired workbench's research/HANDOVER-UI-DISPATCH-V77-20261111.md is the complete artifact and test record.

The exact 21H461 UIKitCore sendEvent call emits debug ID0x2b8701ac through the system kdebug_trace export. EldenUiDispatch uses the reviewed Pagani typefilter/commpage method to observe only SpringBoard and Preferences, for one30-second window started by the existing Settings replay. It does not modify input, user code or clocks. Overflow, changed tracing ownership and emission loss remain visible.

Kernel SHA256: fcb5e29c686477d43fb7220eff79305078e16dbfc243ec29d9f0f319dda8fdf3

Signed image SHA256: 3bc5c53882085a208e5d9c8e3426e7adcced71bab1c1dd98fb70c6a0e4644e81

Session258e98a14abf7380; new argument elden-ui=1; total first-entry arguments1021bytes. Stock floor1738713600@footer0, parent chain3,8-second default boot and900-second recovery deadline are preserved. The first entry retains iPad8,1/J317 and6GiB memory.

Actual source/host model/native code/unchanged original ABI and image-signature checks passed. All ten inspected observation functions and syscall entry paths are scalar; the legacy syscall's argument forwarding explicitly remains inline to prevent newly generated SIMD. Earlier rejected drafts remain archived and were not flashed. Existing v72 producer/report, power35, backlight13 and recovery inputs are unchanged.

No tablet operations occurred. Stay offline until the owner resumes testing. The next functional test combines the existing one-shot Settings input, UIKit receipt, Preferences/IconServices lifecycle and CRC frame. UIKit receipt alone does not prove an application frame or physical touch. Full peripheral Goal remains active; charging and other drivers are incomplete.
