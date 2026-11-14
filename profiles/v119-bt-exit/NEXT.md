# Next native-daemon diagnosis

Exact input is Native17.ORIGINAL_SHA256, no executable modification during this
analysis. v119 observed exit1, no os_reason. Static paths narrow the next capture:

- Direct `_exit` stub0x100708fc8 has one local reference,0x10010498c, with w0=0;
  it does not explain status1 by this local path.
- `_errx` at0x100708fbc has three local callers.0x10009a374 passes0 and
  "No bluetooth on this device.".0x100669c08 passes1 and
  "Failed to register the Bluetooth daemon listeners".0x1004bc45c passes1 and
  "Failed to start the Bluetooth daemon".
- Listener failure is reached from0x10009a2b8 after direct call0x1004733b0.
  That unchanged local function logs notify registration failures but returns1
  on all ordinary paths. This makes that error path less likely, not impossible
  through unrelated corruption or an unexamined external termination path.
- Startup failure at0x1004bc04c follows0x100473b68→0x100568b38. The latter creates
  four real Mach listeners, and returns false if any result is null:
  com.apple.server.bluetooth.le.att.xpc, le.pipe.xpc, general.xpc, classic.xpc.
-0x100568c20 calls `_xpc_connection_create_mach_service` (stub0x100709f34) with
  flag1, sets its handler and resumes only on a non-null connection. The original
  com.apple.bluetoothd plist contains all four MachServices, is mobile/Interactive,
  RunAtLoad and has the unchanged executable path. Presence in the plist does
  not prove the runtime signed service cache/check-in matches.

Next capture should identify the exact errx caller or each real XPC result and
error, retaining the original call/return behavior. The existing kdebug_trace
stub0x1007090dc and kernel observer could carry a bounded signature-matched
marker, if its register/stack/errno preservation is first validated. Do not
substitute success for a failed listener and do not globally disable launch
constraints merely because later relaunches fail category checks.

Private runtime source details and fuller disassemblies remain in workbench
TEMP/edl_work; this map publishes only analysis addresses and provenance. No new
candidate is implied by these leads.
