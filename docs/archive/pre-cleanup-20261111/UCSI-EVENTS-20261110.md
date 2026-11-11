# Complete the stock PPM notification handshake

Use `elden-ucsi-init=2` for the new board baseline. Mode1 preserves the old
basic-notification-only behavior for comparisons.

The stock charger PPM sets its OPM-ready bit only when the notification mask
contains5804. The old8001 mask never qualifies. The pinned TB322FC Linux core
and canonical QcomUcsi both enable the second notification phase and handle
connector events. The board's features0 maps to mandatory maskDA05.

The new sequence discovers both ports, enablesDA05, ACKs the command, then
re-reads the ports before publishing. Connector ACK follows its own successful
status read. Busy/error/wrong-connector replies do not ACK the event; a late
event invalidates PDO consistency before publication. No role, voltage, sink
path or increased-current command was added.

Basic55/power30 was device tested twice:49 no-pulse cycles at5V with2 connector
ACKs, and59 cycles with the existing PD procedure at9V with5 ACKs. Both ended
with pending0, no UCSI errors and zero USB drops. Stock Android/root/green was
restored afterward. This validates the protocol repair, not peak charging or
the unresolved shared500mA input budget.

Source now includes one additional pre-ACK handoff guard, separately tested
and compiled as power31. It is not yet in a linked/device-tested kernel.
Use that archive in the next full build; preserve the v55 source freeze.

Workbench details:
`D:/Desktop/Git/Patch/Y700-Elden/research/HANDOVER-UCSI-EVENTS-20261110.md`.
