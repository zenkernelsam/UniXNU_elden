# Elden Bluetooth firmware adapter

Adapts sheng VFS loading to TB322FC WCN7861 with exactly the five Armada firmware paths.
Runtime APIs sheng_bt_firmware_read/release remain intact. Uses the qualified Elden RAM Data readiness
instead of the GPU addon gate. No startup or HCI download occurs from this archive by itself.
Source: UniXNU-sheng68940017, BSD-2-Clause-Patent. XNU21H461 VFS compile context is required.
Firmware integrity is checked in the staging manifest; live transport and per-device BD_ADDR remain unqualified.

## WCN7861 peripheral protocol

`Wcn7861Peripheral.c` is the board-specific pre-HCI codec adapted from the Armada
TB322FC patch0227 and h0cheung dc7d14/ac6ae99. It validates the entire peripheral
patch/NVM/TME-L container before generating any243-byte download segments. NVM
tag17 second byte becomes8Mbps code0x15; tag27 second byte gets IBS bit0. Only
outgoing copies change, preserving the original firmware files.

The actual Peach20 TME-L asset is ELF32 little-endian RISC-V (machine243). The
codec checks its program-header/file bounds; the board firmware manifest still
has to verify its exact SHA. Control/version/completion/notification codecs
keep peripheral0x31/0x34 separate from ordinary HCI. The four known Peach tuples
use the current UART baud and skip a second ordinary0xfc48 switch.

These helpers contain no transport/startup or automatic initializer. The future
QcaBluetooth adapter must own UART framing, clocks/power, serialized commands,
timeouts and asynchronous notifications. Quarantine a timed-out transaction:
the protocol has no nonce to identify a delayed reply. Ordinary WCN7861 HCI TLV,
the8Mbps GENI resource plan and native iOS17 integration remain to be connected.

Host test `TestWcn7861Peripheral.c` uses the three exact firmware assets and checks
immutable full-file segmentation, malformed late tags/ELF bounds, short outputs,
subsystem/tuple matching and notifications. Compile it only for host tests,
never include its main in a kernel archive. The C codec is GPL-2.0-only; the
public header and existing VFS adapter are BSD-2-Clause-Patent.

## Ordinary HCI continuation

Wcn7861Hci continues the same ready peripheral session without resetting its
H4 framer or dropping queued bytes. Full patch/NVM preflight precedes ownership.
Canonical Pagani EDL encoding sends 243-byte chunks, waits only for the final
patch reply and waits for every NVM reply. Tag17[1], tag27[1] and tag2 change
only in transmitted copies. WCN7861 does not use the older tag17[0] IBS edit.

The executor checks real unified Command Complete, Reset, post-patch ROM/SOC/
patch version, Qualcomm manufacturer and Read_BD_ADDR. A qualified unique
address is required; the shared firmware default is refused. Unexpected,
partial or duplicate completion, short/uncertain writes and timeout poison
the session. There is no retry. Only a physically qualified reset permits a
fresh instance. The transport write must drain the full packet and honor CTS.

TestWcn7861Hci reuses the peripheral controller fixture, then checks the
complete five-asset transition in21 host scenarios. This is model evidence.
ELDEN_BLUETOOTH_HCI_BUILD links the executor and adds both normal assets to
the read-only preflight inspector; it does not invoke startup. Physical power/
UART, native21H461 bluetoothd/UART.SK and persistent pairing remain pending.

## UART14 native backend

UartPlatform/UartBackend implement the actual fixed-board register callbacks
and1x8 FIFO packing, real TX completion, bounded failures and resource return.
The epoch-bound shared WCN power/reset/parent provider must grant access.
Default provider returns-EAGAIN/0; no worker invokes the backend automatically.
This is the internal Bluetooth UART, while PC logging remains DWC3 USB.

15 timed hardware-model scenarios include the full five-asset protocol over
ordinary/DFS clocks, small RX reads, CTS and counter-stall/error cleanup.
Native iOS17 build/link and independent original module/HID/assets/signature
checks pass. Actual hardware/throughput, strong power owner, IRQ/DMA and native
daemon remain required; see docs/WCN-UART-20261108.md.
