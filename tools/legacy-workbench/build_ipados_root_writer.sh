#!/bin/bash
set -euo pipefail
task_workspace=/mnt/d/Desktop/Git/Patch/Y700-Elden
task_sysroot="$task_workspace/research/partclone-deps-arm64/sysroot"
mkdir -p "$task_workspace/edl_work/root-install-20261107"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Wno-deprecated-declarations \
 -I"$task_sysroot/usr/include" -I"$task_sysroot/usr/include/aarch64-linux-gnu" \
 -L"$task_sysroot/usr/lib/aarch64-linux-gnu" \
 "$task_workspace/edl_work/write_ipados_root.c" \
 -o "$task_workspace/edl_work/root-install-20261107/write-ipados-root" -lcrypto -ldl -lpthread
