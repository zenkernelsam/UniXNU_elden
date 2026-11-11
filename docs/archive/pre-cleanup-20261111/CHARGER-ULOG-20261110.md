# Stock charger ULog over the native ADSP transport

Opt-in `elden-ulog=1` adds two GET requests on `PMIC_LOGS_ADSP_APPS`:
owner32778, INIT_GET0x23 and CURRENT_GET0x18, each returning8204 bytes.
There is no log-setting or charger-control SET. It requires power-votes and
excludes the register observer. During PD bootstrap it waits for successful
restore plus10 seconds. Shutdown prevents new queries.

The native implementation uses separate static8204-byte receive storage and
an8192-byte worker payload; the original560-byte transmit/small-message queue
does not grow. Only this opt-in changes the five logs receive intents to8204
bytes. A bad reply stops diagnostics independently of charging. Output is
one64-byte hex row per worker iteration with complete-page CRC32.

Basic51/power26/backlight13 was tested on TB322FC. Both pages were nonempty
and CRC-verified (9bb7ff35 and4c79363e). The1190689-byte USB capture had no
drops/reconnects; PD9V, restore, clean shutdown and automatic fastboot passed.
Exact stock boot was restored and Android/root/green confirmed afterward.

This proves a diagnostic channel, not complete dual-port reverse blocking or
peak charging power. The first pages contain AW35615 chip91 initialization,
charger PMIC7/model56, parsed configuration and both ports' sink-role reports.
They are not the complete boot log; current-page traffic is mostly UCSI queries.

Workbench evidence and recovery details:
`D:/Desktop/Git/Patch/Y700-Elden/research/HANDOVER-CHARGER-ULOG-20261110.md`.
Native source freeze: workbench `research/basic-bundle-20261108/source-compiled-v51`.
Kernel SHA `273f090d8997bde60dae3034b61c323aad44d07a24c332c5c97d00393ec7c6b0`;
signed image SHA `906ee24d0a6dc0fdadbef7157f374a51c6da55292b797bad52b0c898da2cf031`.
All boot rollback values remain1738713600 at footer0/parent3.

## Bounded pagination, basic52/power27

`elden-ulog=2` retains the same read-only transport, with8 init and4 current
page ceilings and a12-request boot cap. Empty pages stop each stream; repeated
text or malformed replies stop diagnostics. Mode1 still uses two pages.
Protocol24643 plus pagination326 sanitizer checks passed.

TB322FC test returned6 init pages (5 nonempty,32612 text bytes) and2 current
pages (1 nonempty); both streams reached an empty terminator before the cap.
All8 pages passed CRC;1328782 USB bytes had zero drops. PD9V, clean restore,
automatic fastboot and full-SHA stock restoration passed;Android/root/green
were confirmed with99%,4463mV and Charging,then the screen was put to sleep.

Initialization records identify platform3,SC8541 at6b/6c,PMIC7 GPIO indices9/10
at initial level0 and OTG/WLSTX disabled. These are initialization records,not
a hardware reverse-current-blocking qualification. Battery authentication is
logged disabled before the UID-read error;do not bypass protections based on it.
Full workbench evidence:research/HANDOVER-ULOG-PAGING-20261110.md.

## Input/GPIO observation and settled failure diagnostics

Basic54/power29 adds a pure GET eligibility gate for advanced diagnostics
after a failed PD attempt has safely settled. Pending/uncertain/pause or
unconfirmed restoration states remain refused. The control admission and
pulse retry policy are unchanged.72 new gate checks and1809 scheduler checks
passed;the specific failed-terminal branch has not yet run on the device.

`elden-regs=2` reads19 fixed registers instead of the legacy four. The actual
stock Root-PD GPIO descriptor hasstride100 and Android base8800:firmware
zero-based indices9/10 are9100/9200. Stock and v54-after9V both show enabled
output mode,VIN1,source0,status80;internalUSB is then suspended withICL0 and
DC is selected at500mA. No GPIO is written by this observer. This is not a
measurement of FET reverse current or switching transients.

The1144556-byte v54 capture had no drops and clean fastboot/stock restoration.
The preceding v53 attempt stayed5V and never reached its observation gate;
itsdrop904 log is preserved. Do not raiseUSB2 current based onAndroid's
current_max900mA field:the actual connection wasHigh-Speed.
Detailed workbench evidence:research/HANDOVER-GPIO-INPUT-20261110.md.
