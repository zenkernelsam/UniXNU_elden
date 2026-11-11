# Experimental Elden PD initialization

The observed stock sequence is documented in the workbench handover. This implementation is opt-in and does not raise the PC's 500 mA USB budget or send unqualified PD setting commands.

`elden-pd-bootstrap=1` verifies a qualified GPIO pulse, USB recovery, a fixed 9 V PD contract and coherent 500 mA readback. Mode 2 adds one 20-second input pause followed by restoration. Both require `elden-thermal=3`, the normal PMIC/ICL/PPM setup, source-PDO queries and an armed autonomous return deadline. Ordinary profiles keep the feature off.

The existing ADC/thermal worker owns the pulse. It requires two valid thermal cycles, both connectors and the battery between 10 and 40 C, initial low levels and adequate time before return. It requests 100 ms high on the measured USB1 cutoff; reopening authorization expires after one second. It does not touch the data mux. Stop-before-start races are closed, and uncertain hardware state retains its lease.

PmicClient is the single executor for the protocol. It checks current-epoch telemetry, separate UCSI freshness, source PDOs, PC data role, battery health and voltage. Two newer snapshots qualify progress. A pulse is requested only once; a failed or stale contract never starts the input pause. The pause uses the existing UsbIcl state machine, so ordinary budget updates do not race an independent writer. Losing conditions cancels the pause, and restoration requires a later GET as well as ACK. Unknown completion is quarantined, not retried.

The v41 live test reached 9 V while the native USB connection remained active. Firmware then reported its 100 mA default, while the driver's cached ACK still said 500 mA. The state machine correctly refused further progress. Power22/v42 reconciles an owned known default only after two newer telemetry cycles; it does not reopen an unexplained zero or replace an arbitrary smaller policy limit.

This native result differs from stock Android, which tears down its data stack on the same cutoff event. The native debug controller uses an inherited device role and VBUS/session-valid override. A short pulse remaining connected does not establish that an arbitrary long cutoff is safe or that all source power is independently metered.

The return path asks this state machine to stop before stopping the thermal worker. The target deadline remains a fallback; it does not make unknown ICL completion successful. Actual field evidence and current device state are kept in the workbench's PD bootstrap handover. Net charging, maximum power, repeated starts and daily-use policy require separate device evidence.
