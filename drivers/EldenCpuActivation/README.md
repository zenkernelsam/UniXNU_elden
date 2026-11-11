# Elden deferred CPU activation

This adapter extends the audited CPU8 ArmCpu input, with a SHA-bound source transform.
Enable `ELDEN_ARMCPU_DEFERRED=1` only in a future explicit CPU profile.
Secondary nubs remain queued until `elden_armcpu_release_secondaries()` is called
and a board-provided `elden_smp_recovery_ready()` hook returns true.
Without that hook, release fails closed. The transform itself neither arms a
watchdog nor changes GIC, PSCI, frequency, PMIC or power settings.

The recovery hook must check current USB/SDAM recovery evidence, a sufficient
remaining test window and a separately qualified independent reset fallback.
A boot argument, a past successful reset or the existing scheduled deadline
does not establish these conditions. The hook must be nonblocking: readiness
is checked at release and between secondary registrations.

Repeated release and concurrent nub arrival use the original atomic pending
exchange, so each queued CPU is consumed once. A release return value of0 means
only that release was accepted, not that any CPU is online. Use the existing
per-core entry/failure/timer/IPI reports and OS scheduling evidence for acceptance.
Only the boot0 topology already established for Y700 is covered by this adapter.

ArmCpu `read32` uses an explicit plain LDR. v83's compiler-generated pre-index
read faulted;v84 read the same GIC snapshots successfully with the fixed form.
Both CPUs have enabled Group1 SGIs and awake redistributors,but IPI delivery
is still unqualified. Keep the architecture review:CTL bit0 is not a missing
Group1 enable. Inspect TYPER2 capability and legal SGI-mode sequencing before
considering another change;do not toggle security or borrow unreserved SPIs.

Current state: v81 is signed and tested with two CPUs. CPU1 reached step7,
executed processor-bound work at MPIDR0x80000100 and received timer interrupts.
IPI counters stayed zero,so full admission failed;the guard stopped petting and
returned to fastboot. Stock Android was restored. Full Goal and all eight cores remain the
target. Two-core admission is performed with a separate topology/profile, not
by falsely reporting eight online CPUs. Paired workbench evidence lives under
`research/cpu-activation-20261111/`.

## Independent fallback work

Current hardware result: v80 passed one single-core RAM Data watchdog
expiration→fastboot test. Keep bark27000ms/bite30000ms and one initial PET.
Qualcomm's actual queue selects BITE immediately for equal deadlines;bit31
therefore does not mean the bite deadline has already elapsed. After the27s
bark,the trial must continue waiting for the30s bite. The full evidence and
limits are in the paired HANDOVER-GUNYAH-RETURN-PASS-20261111.md and
expiration-qualification-v80.json. Sustained keepalive,SMP and180s panic
integration remain unverified on hardware. Frozen v78/v79 failures and their source/host records
remain in the paired workbench; the current source includes the OEM's initial
PET and eight raw reply records to retain failures across cleanup.

The paired workbench confirmed that stock Android actually binds
`gh_virt_wdt` to `hypervisor:qcom,gh-watchdog` and reports it enabled.
The earlier PMK8550 PON candidate is retired: its observed subtype9 is Gen3
and Lenovo's `qpnp_pon_wd_config` explicitly refuses Gen3. Do not enable it
based on an unqualified Linux DTS entry or zero register reads.

`GunyahWatchdog.c` implements the vendor-hypervisor watchdog protocol with a
fixed 27000ms bark/30000ms bite intervals, matching the common OEM/upstream ABI and
remaining below upstream's32768ms reserved range. `GunyahSmc.c` permits only
the four fixed operations and their exact argument forms. It uses SMC, not HVC.
The caller must serialize the context and provide current recovery readiness.
Arm refuses an existing enabled/expired watchdog; ambiguous enable completion
causes one bounded cleanup sequence, whose result remains explicit.
Stop confirms disabled state, not restoration of unreadable previous timeout
configuration. The control core requires separate board integration and
source-bound route evidence.

Host ASan/UBSan, actual XNU ABI objects and the v80 linked expiration trial pass.
Stock Android was fully restored afterward. The new controller and live
recovery-ready hook are linked and exercised in v81. Repair and qualify IPI
before increasing the enabled CPU count.
Keep180-second panic policy unchanged until its interaction with a30-second
fallback is deliberately integrated; these deadlines are not interchangeable.

## Guarded admission

`vdog=2` selects the bounded admission controller. It requires explicit
`cpus=2..8 sheng-smp=1 armcpu-self-ipi=1`,USB/SDAM/RAM Data readiness and the
qualified watchdog protocol before granting a one-second release lease.
`GuardPolicy.h` requires online CPUs,processor-bound work and timer/SGI progress,
then five seconds of stability. It stops feeding on failure,completion or lost
readiness;the watchdog remains active through a potentially stalled shutdown.
Panic feeding uses only PET/STATUS for the owned instance,without mutating the
normal worker's protocol context. The two-core work/timer/failure-return paths
are exercised;panic feeding remains untested. See paired
HANDOVER-CPU2-IPI-20261111.md for evidence and the current IPI blocker.
