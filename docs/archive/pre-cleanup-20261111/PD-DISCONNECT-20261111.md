# PD cable comparison, 2026-11-11

The owner confirms both meter rounds ran in iPadOS, followed by a return to fastboot. Short-port PD alone measured8.45V/1.76A and8.46V/1.85A (about14.9-15.7W). With the long PC cable connected, short-port input was near0W; one extra reconnection showed8.86V/0A. These are input readings at near-full SOC, not net battery charging or peak-power qualification.

The existing power35 candidate e1edfb1c/sessionce209214abfedb57 reached9V and phase6/error0/restored1 with the pulse low and20-second pause/restoration recorded.290379bytes were captured with no reported drops before USB loss; reconnect timed out after60seconds. Multiple physical cycles and reconnect dwell times were not timestamped. Post-disconnect votes, continued log integrity and the exact reset trigger remain unverified; do not infer a USB-driver root cause solely from the host timeout.

Stock boot8b8e72da was fully SHA restored. Android/root/green/boot_completed1,99%/4464mV/-420mA/27.9C/Charging, screen off. No device jobs remain. Current reference is stock; v77 is still unflashed. The paired workbench research/HANDOVER-PD-DISCONNECT-20261111.md contains raw readings and all artifact identities. Continue investigating OEM input priority; do not raise the PC SDP budget or retry retired DC write paths. Full peripheral Goal remains active.
