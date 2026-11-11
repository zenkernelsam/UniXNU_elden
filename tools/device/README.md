# Maintained device transport tools

The existing AVB admission, Firehose buffering/delta transport, full-image writer
and USB console now live here. The writer requires an explicit `--workbench`;
the console requires an explicit `--output`. Neither stores runtime logs in the
source checkout. The old workbench entrypoints are compatibility launchers.

These tools retain this owner's exact device selection and stock signing-floor
contract. They are not generic flash utilities for a different tablet. Moving
their paths does not authorize a device operation or replace candidate review.
No device was accessed during migration.

Offline example (omit `--write`):

```powershell
py -3.12 tools/device/flash_verified_boot.py --workbench D:\Desktop\Git\Patch\Y700-Elden candidate.img reference.img
```

A live operation still requires independent full SHA, fresh power admission,
verified target/GPT and complete post-write readback. See the current maintenance
state before using it. The writer refuses `--write` while the maintenance marker
exists. Dependencies are Python 3.12, pyserial, pycryptodome and Pillow.

The portable CLI path has changed; transport/control bodies are preserved.
Local model tests cover battery admission, port ownership/readiness, session-bound
commands, framing and reconnect behavior. They do not prove physical USB behavior.
