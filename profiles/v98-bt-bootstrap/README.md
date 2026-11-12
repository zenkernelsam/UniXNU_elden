# v98 Bluetooth bootstrap preparation — offline, not installed

This extends v97 with a read-only kernel bootstrap status and an owned BlueTool
XPC service. It preserves the original21H461 bluetoothd; no F2 return-success
patch or F3 transport patch was applied. It is not yet a complete Bluetooth runtime.

## Why this implementation

Sheng's reported successful native Bluetooth/pairing path is iPadOS27/24A446.
Its QcaBluetooth BlueTool service is a useful proven design, but that daemon's
arm64e adapter and offsets are not a17.7.11 recipe. Pagani's21H461 F2/F3 proposal
explicitly labels its native-host work untested. Neither is relabelled as an
Elden hardware success.

The actual21H461 input is SHA
`3f1e4493ea6a76976625f7af58a8f330c0c90d82d9020b11110b4f3f5b758df4`,
UUID `0f836381-d07b-36e4-9dbb-8a44782b9c14`, ordinary **arm64**, chained-pointer
format6 (`DYLD_CHAINED_PTR_64_OFFSET`). It is not arm64e. The final service
matches arm64/iOS17.0; preliminary arm64e artifacts are not deployment inputs.

`client-abi.json` and `tools/audit_bluetool17_abi.py` verify the original
ADRP/ADD argument loads, import-call targets and message-key rebases:

- Mach service `com.apple.BlueTool`, message `RunBuiltinScript`, envelope
  `kMsgId`/`kMsgArgs`, and `script` argument.
- Replies read `result` and `manufacturer` as int64 and `firmware` as a string.
- Optional `btaddress` is generated as `%02X:%02X:%02X:%02X:%02X:%02X`.

The original F2 and F3 bytes remain untouched. Protocol matching does not prove
the native chipset factory, Skywalk path or optional HCI commands are compatible.

## Kernel and service contract

`kern.elden_bt_bootstrap` returns a32-byte versioned snapshot: state, epoch,
manufacturer, patch revision and display-order address. All bytes are initialized;
the single publisher and sequence checks prevent torn status. Reads do not
start, stop or otherwise control hardware; writes are refused.

Only real HCI completion supplies identity, including verified Read_BD_ADDR.
The host/control service must also be published and not withdrawing before the
snapshot reports ready. Stop requests project a stopping state; only verified
radio reset/UART release can publish stopped. An unstarted or survey-only
driver reports unavailable, which does not assert physical power-off.

BlueTool accepts boot/init only with the correct snapshot, live identity and,
when provided, an address matching the actual controller. Read failure, EPERM,
wrong version/size, reserved bits, absent identity and address mismatch do not
become success. It never programs a requested address. Already-stopped hardware
can acknowledge deepsleep; **active-controller deepsleep is unsupported**, not
reported as successful. Runtime power toggle and low-power ownership remain work.

## Validation and outputs

- 74 ASan/UBSan reply/state/address cases passed, including truncated snapshots,
  lower/uppercase addresses, malformed input and stop/identity transitions.
- 20 added objects plus two existing startup/recovery objects compiled and
  linked/prelinked. New kernel objects are scalar.30 original Apple text ranges
  and the five full unique firmware copies remain intact.
- The sysctl adds a normal XNU early-registration entry. Its private-external
  OID localizes at link time; this exact exception is checked, without relaxing
  the public-export contract for other symbols.
- Kernel SHA: `a61abbfd7b2c8bc8221b271d8c7b494934d5eea55fa0220de369cc54abf43669`.
- Final BlueTool SHA: `464432d5308fa0998f9b25ed77ff0c2b9a7751377dc0cc992679a32dc7cc571e`;
  CDHash: `e88b9aee568d043baf7a20caf4696c051b2ca802`. Its signature and actual
  Mach-O architecture/minimum OS were checked; this is not device trust acceptance.

Windows artifacts are under the paired workbench `edl_work/bt-v98-20261112/`.
Mac final outputs are project-local `lab/bt-v98-20261112/kernel-build3/` and
`userland3/`. Raw kernel, helper and job have **not** been installed, boot-signed,
added to the live trust cache or run on the tablet. v94/v95/v96 boot candidates
and the last verified stock Android state are unchanged.

## Reproduction and next work

Use `tools/build_wcn_kernel.py --profile v98-bt-bootstrap` with the same explicit
parent/dependency/firmware/XNU/LLVM arguments as v97, then
`tools/audit_wcn_kernel.py --profile v98-bt-bootstrap`. Build the owned service
with `tools/build_elden_bluetool.py --output NEW_DIRECTORY`; no Apple binary is
needed to build it. Audit the original client separately with
`tools/audit_bluetool17_abi.py --bluetoothd ORIGINAL --output NEW_JSON`.

Next: complete signed runtime/trust and launchd-cache integration, then resolve
21H461 transport/chipset routing using its exact code. The examined launchd
disable policy only disables watchdogd/locationd/nehelper, not bluetoothd;
the saved original bluetoothd job has RunAtLoad and no Disabled key. Do not
blindly remove a supposed Bluetooth disable entry based on an older handover.
Actual WCN/HCI, XPC initialization, pairing, persistence and sleep still require
hardware evidence. GPU stays last, and speaker gain remains untouched.
