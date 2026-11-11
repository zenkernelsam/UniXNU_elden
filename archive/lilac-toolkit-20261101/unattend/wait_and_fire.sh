#!/bin/bash
# Wait for lilac to return to fastboot (kernel self-reset), then fire an image
# once and capture the log from the first line. Unattended-safe: only boots
# the given (level <=10) image; never writes UFS/PSHOLD.
# usage: wait_and_fire.sh <img> <tag> [max_minutes]
set -u
cd "$(dirname "$0")/.." || exit 1
IMG="${1:?usage: wait_and_fire.sh <img> <tag> [max_minutes]}"
TAG="${2:-retry}"
MAX="${3:-25}"
for i in $(seq 1 $((MAX * 3))); do
	if fastboot devices 2>/dev/null | grep -q BH905ND69G; then
		echo "fastboot seen after ~$((i * 20))s; firing $IMG"
		bash research/run_v238.sh "$IMG" "$TAG"
		exit $?
	fi
	sleep 20
done
echo "no fastboot within ${MAX}min; giving up"
exit 1
