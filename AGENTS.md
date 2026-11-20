# UniXNU elden — authoritative offline peripheral source

Branch `offline-peripheral-fixes`, from committed main@f82da007. Read
docs/peripherals/STATUS.md, INDEX.md and MAINLINE-HANDOFF-PROMPT.md.
Active full scope: AP501U pen, calibrated iOS effects for dual AW86937,
RTC/wake, accel/gyro rotation, front/rear cameras. The five-part Goal stays
active until every objective is achieved; component success is not global
acceptance. No tablet/ADB/fastboot/flashing/QEMU, automatic merge or subagents.
Mainline owns hardware tests and Wi-Fi/PCIe/WCN/QRTR; GPU remains separate.

Current camera-frame implementation f3329c9: real originalDMA/Board/CCI/CAMSS
completion→visibility→CPUborrow→cachedsnapshot→explicitreturn/requeue,
read-onlyOS17IOKitservice and signedfront/rearCMIO/RAW10 preview. Source11/
host13/native11/kernel11/stack10/provider9/all11finalaudits accepted;
kernele6459d9c/native70e4c098/2scalar0warnings/1069RAW10cases6062checks.
Physical IOKit/PAR/cache/clock/GIC/MMIO/firmware modeled; actualoriginalpower/
PLL/RPMh/CSIrails/earlyauthority/DMA/RAW10/calibration/ISP/appacceptance missing.
Noauto-start/jobcachetrustinstall/device/merge. CAMERA-FRAMES.md and
frames-validation.json contain exact inputs/artifacts. Parent sharedSMMU
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
