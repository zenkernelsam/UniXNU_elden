# Typed USB limits and PDO notifications

Power34 adds opt-in elden-usb-icl=2: only fresh same-epoch SDP consumer snapshots with matching adapter1/real1 can issue USB current SETs. Mixed,PD and stale snapshots defer without claiming a restore or deleting ownership. This follows sheng firmware ownership for PD and the exact stock Y700 SET semantics;the stock PD branch does not interpret100mA as an ordinary fixed reduction.35 new,77 old,and318 existing bootstrap sanitizer checks pass.

Power35 applies the known v1.1/two-port/features0 PDO query quirk to Linux PDO-details notifications too:CAP_CHANGE/PWR_LEVEL_CHANGE,DA65 instead ofDA05. Raw capabilities remain raw,and no new voltage/current/role/sink command is introduced.431 event,1845 legacy,and796 PDO sanitizer checks/native no-SIMD/archive/ABI/AVB pass.

V58 with large diagnostics saw9V but dropped57685B;its quiet repeat exercised the typed guard yet did not complete9V. V59 quiet capture247405B/drop0 completedDA65/ACK6,power-change flag clearing,9V contract andWLS8.696-8.715V,500->0->500,phase6/restored1,sync/fastboot and full stock Android/root/green restoration. One complete run proves this combination worked here,not full repeatability or maximum charging. Last10 aggregate-current median+144.5mA was still discharge.

Keep typedSDP2 and the PDO notification fix for the next qualified profiles. The fixed DC500mA same-value WRITE still lacks hardware qualification;larger current and full dual-port electrical recovery remain separate work. No forced FETs,parallel VBUS,unqualified host budget,or speaker activation.

Full paired-workbench record:research/HANDOVER-PD-HANDOFF-20261110.md;frozen sources:research/basic-bundle-20261108/source-compiled-v59.
