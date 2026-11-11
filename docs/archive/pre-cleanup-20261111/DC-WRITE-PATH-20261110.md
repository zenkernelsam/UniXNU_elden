# DC access-path evidence

V60 adds raw WRITE response diagnostics and a read-only SPMI core/owner-table survey.2033 protocol checks and native object/no-SIMD/ABI/AVB validation pass;existing control code is unchanged after removing only the diagnostic additions.

The actual capture244078B/drop0 returned16B/type1/op38/status1 after the fixed19-register pre-read. This is a failed WRITE,not a successful same-value qualification. The exact signed firmware has a generic-test write gate consistent with status1;its conditional hardware bit was not read. Do not alter this gate,fuses,ACLs or signed firmware.

PPID727/729/72a each have a unique EE0-owned APID702/704/705 plus a foreign EE3 instance552/554/555. The survey did not map/command observer or writer channels. Normal AP-owned SPMI is the next candidate path;ownership alone does not prove register writes. Revalidate live mapping/ownership peroperation and preserve other owners.

Cross-check native readback,then qualify bounded500->450->500mA (raw10->9->10) with real hardware completion and readback,source/thermal/vote gates,stop and restoration. Do not fabricate GLINK ACKs or increase beyond the known500mA before effective control is proven.

9V and the existing recovery path remained healthy;exact stock Android/fullSHA/root/green was restored. Full charging/peripheral goal remains incomplete. Detailed workbench handover:research/HANDOVER-DC-WRITE-PATH-20261110.md.
