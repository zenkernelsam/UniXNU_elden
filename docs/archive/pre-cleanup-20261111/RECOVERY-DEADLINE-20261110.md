# Target return deadline and USB capture continuity

This is an opt-in diagnostic facility for Elden's qualified volatile RAM Data configuration. It does not complete PD charging or peripheral support.

`elden-autoreturn=N` accepts 90 through 900 seconds. The deadline starts when the measured SDAM fastboot route is armed. At expiry, the target checks the 512 MiB RAM Data and cache qualifications, claims shutdown ownership, stops the existing touch/thermal workers, performs the normal filesystem shutdown, and takes the existing SDAM/PSCI fastboot path. No USB ACK is required for this entry.

The normal RPC entry retains its ACK requirement. Shared atomic ownership ensures that a deadline can supersede an uncommitted ACK without a late RPC cancellation undoing the shutdown. Only one entry commits the shutdown. Persistent Data is not admitted by the deadline entry.

This is a software thread deadline, not a hardware watchdog. It depends on the kernel still scheduling and on the qualified shutdown path completing. Lost qualification produces a recorded hold. Future charging experiments must finish or cancel their own control operations before this shutdown path proceeds.

USB status includes `boot=<16 hex digits>` generated once from the kernel early-random API. The existing session is a static image identity; the additional token distinguishes observed boots during reconnect. Neither field authenticates an untrusted USB device.

The Windows logger's optional `--reconnect-seconds N` uses fresh VID/PID/serial and physical-location checks, then requires the same image session and boot token. It clears the healthy-status interval after a gap and never repeats an already-attempted control request. Reconnection is bounded by the original capture duration and at most three reopenings. Host charge sequences are not admitted with reconnect until their separate transaction recovery has been qualified.

Raw capture bytes are preserved. Gaps are recorded as offsets in the adjacent JSON. The frame decoder reads this metadata and only accepts images that begin and finish within a continuous segment and pass all row/CRC checks.

Host validation covers the actual ownership header in 1,000 ACK/deadline/cancel races with ASan/UBSan, and 14 logger, identity, control, and frame checks. An initial native build was rejected for compiler-generated SIMD in an error path; the recovery-bearing UFS/USB build now uses general registers only. Native object/archive and original module/ABI checks remain required.

Workbench evidence is under `research/recovery-deadline-20261110/` and the v40 test directories. Device outcomes and the currently restored image are recorded in the workbench handover; do not infer success from this interface description alone.
