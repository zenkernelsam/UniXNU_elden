# UniXNU elden — authoritative offline peripheral source

Branch `offline-peripheral-fixes`, from committed main@f82da007. Read
docs/peripherals/STATUS.md, INDEX.md and MAINLINE-HANDOFF-PROMPT.md.
Active full scope: AP501U pen, calibrated iOS effects for dual AW86937,
RTC/wake, accel/gyro rotation, front/rear cameras. The five-part Goal stays
active until every objective is achieved; component success is not global
acceptance. No tablet/ADB/fastboot/flashing/QEMU, automatic merge or subagents.
Mainline owns hardware tests and Wi-Fi/PCIe/WCN/QRTR; GPU remains separate.

Current camera delivery56c22f7 adds CSID IRQ→retained originalexecutor wait/
service→actual DMA/snapshot pump over Bayer/latest fixc15d94e. source5/frames5/
native5/frames-native5/stack5/bridge5/kernel5/input-gates9/irq-audit10 plus
elevenfinal-audits5 accepted;218core1202627/18native/20inputgates,
front/rearfourframes normal63174/drain63382/visibility61846; actualOSclient/
RAW10each1382433checks. Kerneld7fc72c7/nativefa0b40a8; scalar0warnings.
IRQ onlymask/latch/wake, exact rising-edge CSID. No GICpendingclear on rearm;
wait has no sleepable provider afterassert_wait; attach/stop/active races
retain/refuse. Explicit currentoriginal executor enable_irq/pump_irq only;
no auto caller/thread/start. OnlyNativePipeline.o/FrameOutput.o replaced,
Service retained after loaded local/global symbols/relocs/bytes comparison.
Physical IOKit/PAR/cache/clock/GIC/MMIO/firmware modeled; actualoriginalpower/
PLL/RPMh/CSIrails/earlyauthority/DMA/RAW10/calibration/ISP/appacceptance missing.
Noauto-start/jobcachetrustinstall/device/merge. CAMERA-IRQ.md and
irq-validation.json contain current inputs/artifacts. CAMERA-FRAMES.md,
bridge-validation.json/frames-validation.json retain interface/history details. Parent sharedSMMU
d7571f2/source12-host12-stack13-native12-kernel12:173core1400032+
11composed2395232,actualpostrouteTTBR/tablegraphcheck BEFOREbatchpublication.
Read SHARED-SMMU.md. Oneoriginal4K/SMMU/global97 provider; no lateappend/no-op/
fixedbank. Alloriginalarchives/recovery5500ms/Apple30/floor gates preserved.

Board sources: exact ArmadaOS TB322FC/SM8750 first, frozenLinux6f97250e;
then Linuxmainline. OS17 interfaces require qualified same-version donors.
Do not copy TB321FU/SM8650 or TB323FU/SM8850 register/firmware identities.
No fabricated samples/pixels/calibration/RTCepoch/retirement. Motor conversion
requires measured gain/boost/frequency/duty/thermal/dual-load limits; unknown
formats/output refuse. AHAP/intensity/Applewaveform is not hardware voltage.

Code/tools here; raw evidence in workbenchlab/edl_work, researchGit-only,
one-off scripts in WindowsTemp. Preserve peer edits/staging; noreset/stash.
Mac checkout is isolated and source/hash-qualified. Check workbench
maintenance/IN_PROGRESS.json before shared writes; no concurrent reference
checkout or changing frozen candidate inputs from sync. Retirement only via
nativeTrash/WindowsRecycleBin; never permanent delete or emptyTrash.
Keep original physical owners/recovery/floor1738713600@0/parent3. Whole
old-kernel artifacts must not overwrite mainline; port/rebuild its current source.

Remaining: actualphysicalstartup/DMA; penPencil/palm/tilt acceptance;
motionsame-devicePD/registry/calibration/mount/time/SUID/samples/SPU/UI;
realdisplay/systemPMwake; measuredsystemhaptics;RTCpersistence/alarm;
cameraRAW10/ISP/CMIO/app. Preserve mainlineWiFiQRTR/GPU/recovery ownership.
Historical turns are archived under docs/archive/peripherals-20261121.
