# Dual AW86937 and iOS effects — offline driver chain

The actual Gen4 AW driver, two-chip GENI bus, calibrated queue, Apple importer
and native SE6/worker/root-only user client now compile and fully link offline.
Current native details: [HAPTICS-NATIVE.md](HAPTICS-NATIVE.md) and
`drivers/EldenHaptics/native-validation.json`. The portable milestone is in
`validation.json`; raw evidence/assets stay in the workbench. Continuous RTP,
RTP/controller and explicit compiler update is in [HAPTICS-RTP.md](HAPTICS-RTP.md).
Measured bank, native request/catalog/shared budget and system Actuator remain.

## Source and transport

Board facts are fixed TB322FC/SM8750 Linux6f97250e: AW86937 at0x5a/0x5b,
wrapper1 SE6 at0xa98000/0x4000, SPI363/INTID395, SDA56/SCL57 mux1/2mA/pull-up,
left/right active-low reset127/119 and falling completion99/95. No speaker
driver or guessed older-generation registers are used.

`AwSource.py` extracts the actual GPL Gen4 init/STOP/RAM functions with an
explicit shim and owning-bus callbacks. Only playback-body change is loop15 to
single-shot0. Identity must be0x9370; native firmware identity must match1436B
SHA84598d2a. Init sets gain0, uses dedicated STOP, then Gen4 configuration/full
SRAM write-readback, without playback. The selected Linux boost is7V, not the
Android8500mV report interpreted as a limit. No live boost/calibration acceptance.

`GeniSource.py` retains Pagani's BSD GENI state machine under an Elden namespace.
One serialized bus handles both chips; writes split at7 payload bytes and reads
at4. RAM0x42/RTP0x32 do not increment their port address. Failed reads publish no
partial buffer. Acquire refuses active DMA/GSI; external SE IRQ output is
suppressed, metadata restored/read back with output last. Timeout requires actual
cancel/abort acknowledgement and idle; uncertainty holds the lease until reset.

Touch's two wrapper1 vote RMW operations now use the same GCC guard. Both
touch/haptics preserve shared wrapper votes while a sibling SE remains active.
The native link replaces the exact touch platform rather than assuming the
old unguarded source participated. Current-mainline port remains separate.

## Requests and effect data

`Queue.c` copies measured per-motor gain LUTs and supports32 owner/epoch/id
requests, real start/poll/STOP outcomes, cancel/disconnect and deadlines. Duration
is capped at250ms and the actual waveform length. A failed GO still receives an
owned STOP attempt. Rolling64-entry budgets expire after each request end plus
window, preventing fixed-window double bursts. Gain-squared budgets are a proxy,
not a temperature or mechanical qualification.

The importer reads locally owned iPhone patterns and17 AHAP, retaining142 recipe
timings/intensities/envelopes/frequencies, Tap/Minitap/Sine types and the original
`Ampliude` spelling. AHAP trailing commas are accepted only outside strings;
duplicate/nonfinite/unknown data is rejected. AudioCustom CAF/WAV sidecars are
recorded and never mistaken for motor samples. Unknown waveform/envelope units
are not invented. `SampleMap.c` maps signed samples through a measured LUT with
gain/peak/sample-rate/frame/squared-budget limits and atomic failure.

## Accepted offline evidence and missing work

Actual extracted AW/init/readback/fault paths pass126515 checks; the actual
GENI→AW→two-chip composition passes52762; queue154. Apple import1467 and all65536
signed conversions pass. Native scalar objects compile. Hardware/MMIO/IOKit
peers remain modeled and no real motor is driven by these checks.

Native platform/worker/user client is now linked and separately checked as
documented above. Next: native RTP requests/catalog/shared budget, system Actuator
and measured templates. Keep calibration/firmware/stop gates/default-off behavior. The
requested haptic scope includes continuous effects and system interaction;
single RAM clicks alone do not complete it. Mainline owns calibrated motor
trials, current source integration, power/temperature and combined-load checks.
