#!/bin/bash
# Python admission runs before the first ADB call. --dry-run is offline.
set -euo pipefail
ELDEN_SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
MSYS_NO_PATHCONV=1 exec python "$ELDEN_SCRIPT_DIR/flash_adb.py" "$@"
