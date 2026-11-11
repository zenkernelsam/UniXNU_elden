#!/usr/bin/env bash
# DEPRECATED — DO NOT RUN.
#
# Goal was: arm PM8998 PON SOFT_RB_SPARE(0x88f)=BOOTLOADER via xperable -w
# (ARB_WR chnls @0x8400000) so ABL idle/reset returns to fastboot.
#
# KILLED 2026-10-24: xperable -r/-w executes inside patched-ABL context.
# A DDR read works, but an MMIO access (0x800f000 probe) crashes ABL ->
# reset -> charge mode (E1F4). Done it twice, both times device needed a
# physical button. xperable memory access = DDR ONLY.
#
# PON arming therefore stays where it works: shim-side SPMI (RET=1 path,
# shim.c spmi_write_apid(4,0x88f,0x06) before loader_boot).
#
# Protecting a bare fastboot session from ABL idle timeout remains OPEN:
# candidate = periodic harmless fastboot cmds (keep-alive) from the host.
echo "arm-pon-fastboot.sh is retired — xperable MMIO writes crash ABL (see header)." >&2
exit 1
