# v124 exact-owned BlueTool launch

v123 fixes `bluetoothd`'s first exit, but the full capture contains103 attempts
to launch `/usr/sbin/BlueTool`, all rejected with
`c[8]p[1]m[1]e[6], (Constraint not matched)`. This is a different process from
the now-running daemon. See [prior-v123-blocker.json](prior-v123-blocker.json).
Do not describe the absence of daemon LC retries as absence of all LC failures.

Sheng's successful27 runtime uses `no-lc=1` in `board.toml`; its BlueTool also
overrides four launchd lifecycle fields. Elden already has an exact-owner
IconServices exception for this observed RAM-overlay rejection. This candidate
extends that local mechanism to the unchanged owned BlueTool, keeping the current
original561-job cache. It does not assume the remaining lifecycle is accepted.

`drivers/EldenBluetooth/runtime/OwnerLaunch.inc` requires all of:

- Process name `BlueTool`, observed EPERM/reason namespace/code/text;
- Successful prior MAC checks and `CS_VALID|CS_SIGNED`;
- System spawn type1 and successful qualified Bluetooth runtime staging;
- WCN mode4, regular file on read-only union bindfs, exact `/usr/sbin/BlueTool`;
- CDHash `e88b9aee568d043baf7a20caf4696c051b2ca802`.

Other launch failures and subsequent signature/code-page checks keep their
original behavior. No global launch-constraint flag is set. No HCI reply,
hardware state or Bluetooth capability is synthesized.

`tools/test_bluetool_launch.py --output NEW` compiles the actual predicate with
kernel-state doubles under ASan/UBSan, rejects each failed boundary and accepts
the exact fixture. One `kern_exec.o` is rebuilt through the clean source overlay;
all other linked inputs and all30 original Apple executable ranges are retained.
The first build script's CRLF bytes were normalized before publication, followed
by a fresh second build. Only that second result is packaged. Mode2 menu, runtime,
Data/trust and firmware are unchanged from v123; floor1738713600/footer0/parent3.

From a checkout with declared original inputs:

```sh
python3 -B tools/build_clean.py --variant bluetooth --bt-exit-stack \
  --software-keystore --bluetool-launch --inputs inputs.json --output NEW --target boot
```

The full clean baseline and each appended stage have been executed separately.
The combined new option has not been rerun as another complete empty-directory
build. The direct delta entry is `tools/build_bluetool_launch.py --clean-build
CLEAN_OUTPUT --keystore-build KEY_OUTPUT --original-kernelcache ORIGINAL --llvm
LLVM_DIR --output NEW`; both parents come from the repository source pipeline.

Boot SHA `a79c1c7ae678565fe7cecad27cc1b30ab9604ac4760bb47ffc586f5ba9609e68`,
session `b1a78d04dec91467`.

## Hardware result

The observed exact-owner exception accepts PID126 once; no repeated BlueTool
LC loop was captured afterward. This only proves crossing that launch check,
not that BlueTool has handled XPC. HCI startup again reaches ready1/958 segments,
and the daemon reaches its message loop. At108145 captured bytes, USB writes
time out; bounded reconnect expires. No H4 connection is captured. Reported drop0
before the discontinuity is not a complete observation of the later device.

The device autonomously reappears in physical fastboot; the exact reset cause is
unproven because neither sync-return nor SMP-end was captured. Full stock SHA
restore, Android/root/green and boot completion pass; sleep is requested. The
candidate is kept for diagnosis and does not replace the stable baseline.
See [hardware-result.json](hardware-result.json). Next add bounded phases to the
bootstrap sysctl and native connect/send paths. A USB timeout alone must not be
reported as a proven kernel deadlock or a proven transport connection failure.
