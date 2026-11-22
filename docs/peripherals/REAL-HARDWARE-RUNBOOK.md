# Y700 Real-Hardware Acceptance Runbook

This runbook applies after the current mainline has ported the offline source
against its own owners. It is a test order and evidence checklist. It is not
a boot image and it does not authorize flashing.

## Before every session

- Keep the stock recovery path, floor `1738713600@0`, parent `3`, UFS and Wi-Fi
  owners intact. Use a readback and hash receipt for every candidate.
- Keep the long USB data connection stable. Use the short port for charging
  only when the measured setup is known to be safe. Stop if either port,
  cable, connector or charger becomes warm or current changes unexpectedly.
- Start the PC UART/USB listener before entering the kernel. Save the complete
  boot, panic, provider and return-to-fastboot log with a timestamp.
- Confirm the candidate is default-off for pen, haptics, RTC persistence,
  wake-power, motion and camera. Enable one component per session.

## Component order

1. **RTC and wake**. Read the PMK counter and SPMI status. Verify the named
   NVMEM and alarm providers, owner/epoch and IRQ identity before any write.
   Use a short future alarm, then a suspend/wake cycle. Abort on counter
   rollback, unexpected register access, missing panel readiness or a wake
   without the matching generation.
2. **Motion**. Capture real ADSP/SSC registry, SUID, mount, units and timebase
   records before requesting samples. Validate static gravity and rotation
   with the display stationary. Abort on missing calibration, invalid time,
   foreign provider identity or fabricated samples.
3. **Pen**. Capture raw NT36536 packets with fingers idle, hover, tip down,
   lift, barrel button and simultaneous finger contact. Check the actual HID
   service and PencilKit/Notes behavior. Do not apply donor panel geometry or
   BLE pressure merging. Abort on unexpected scan mode, malformed tail bytes,
   stuck tip, missing release or a foreign worker context.
4. **Camera**. Test one sensor at a time. Prove real power/PLL/CCI/CSID/DMA
   readiness, one RAW10 frame, queue retirement and the next frame. Verify
   front/rear identity and CMIO ownership. Abort on synthetic pixels, missing
   IRQ retirement, stale tickets, SMMU route changes or automatic start.
5. **Haptics last**. Do not enable output until each AW86937 motor has a
   measured gain, boost, resonance, duty, current and thermal profile. Bind
   the dual profile, start at the lowest measured gain, and test one motor
   before dual load. Confirm STOP, standby and FIFO acknowledgement after
   every effect. Abort on any uncertain stop, over-current, thermal rise,
   busy retry that replays bytes, or unconfirmed FIFO tail.

## Evidence required for acceptance

Keep raw UART logs, register readbacks, provider identity/epoch, source and
image hashes, power/current measurements, and the user-visible result for
each component. A host test or linked object proves only its source contract.
Set a component hardware flag only after its row in
`GOAL-ACCEPTANCE-MATRIX.md` has all required Y700 evidence. The five-part Goal
remains active until every row passes.

## Handoff and worker workflow

1. Read `AGENTS.md` in the mainline checkout
   (`D:DesktopGitUniXNU_elden`), the `offline-peripheral-fixes` worktree,
   and the workbench (`D:DesktopGitPatchY700-Elden`). Then read
   [STATUS](STATUS.md), [INDEX](INDEX.md), the
   [mainline handoff](MAINLINE-HANDOFF-PROMPT.md), and the
   [acceptance matrix](GOAL-ACCEPTANCE-MATRIX.md). Record the source and
   destination commits before porting.
2. Use the independent Git clones under the workbench's `research/`. Start
   with proven UniXNU sheng driver interfaces and the exact ArmadaOS-Freedom
   TB322FC/SM8750 board sources for registers and wiring. Use Linux mainline
   as the secondary board reference and match OS17 donors to the target ABI.
   See `lab/reference-reviews/20261122-unixnu-reuse/README.md` for reusable
   projects and qualified revisions.
3. Use the `ciscomac` SSH alias for CiscoMac, the Hackintosh build host.
   Its mainline checkout is `/Users/ciscohe/Desktop/UniXNU_elden`. The
   `UniXNU_elden-offline-peripheral-fixes` and `UniXNU_elden-gpu-offline`
   siblings are isolated checkouts. Verify the commit, toolchain, and frozen
   inputs against the build receipt. Use Windows and WSL for host checks.
4. Assign one reviewable port, build, or log-analysis task to
   `Devin-worker-agent-Y700` at a time. Create `_relay/<round>/task.md`
   in the workbench with the source/destination commits, exact writable
   worktree, acceptance criteria, and required build and test commands.
   Give Devin write scope for that worktree and its relay directory. Keep
   logs, input/output SHA-256 values, exit codes, and remaining gaps in
   `_relay/<round>/handover-to-codex.md` and adjacent evidence files. Use
   the PowerShell Devin console's current PID in the dispatch command:

   ```powershell
   py -3.12 'D:\Desktop\Git\Patch\Y700-Elden\_relay\dispatch.py' --pid <worker-pid> <round>
   ```

   Wait three minutes between checks until the handover ends with
   `(handover to codex now)`. Copy compact acceptance evidence to
   `lab/records/` before retiring relay artifacts.
5. The mainline agent owns device trials, diff and evidence review, and
   commits and pushes. Have Devin prepare source ports and reproduce builds
   on the assigned integration branch based on current mainline. Rebuild
   with the current shared owners, repeat the component and recovery checks,
   and record functional Y700 results for each component before merging
   the reviewed source. Keep offline fixes on `offline-peripheral-fixes`
   and workbench evidence in Patch.

## GPU handoff

A830/SM8750 was Elden's first offline peripheral delivery. Its branch is
`codex/elden-gpu-offline`. The accepted CiscoMac build is `complete-3`
and the delivery archive is `edl_work/gpu-complete-20261116/elden-a830-offline.tar.gz`.
Use the GPU branch's [build status](https://github.com/zenkernelsam/UniXNU_elden/blob/codex/elden-gpu-offline/docs/GPU-GOAL-STATUS.md)
and [merge guide](https://github.com/zenkernelsam/UniXNU_elden/blob/codex/elden-gpu-offline/docs/GPU-MERGE.md)
for the pinned Turnip/metal2vulkan inputs and A830-specific owner, recovery,
and activation requirements. Keep the GPU integration as a separate round.
Real GPU startup, command submission, rendering, and recovery still require
Y700 evidence.
