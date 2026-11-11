# v77 UIKit delivery observation

Two boots reused the same signed v77 image; only the first was flashed. The first Home-based attempt remained on the lock screen. The second used the existing software swipe, reached the Home Screen, and issued one fixed Settings tap after the assistant visually checked the CRC-verified gear bounds. The automatic template did not pass; the watcher was stopped before manual continuation.

The30-second window recorded four SpringBoard events in two duplicate-timestamp groups. Kernel publication down-to-up was106485us. The groups arrived3961974us apart. Their original timestamp delta2044422 corresponds to106480.3us at19.2MHz. This supports delivery/processing latency, not a unique CPU, lock, IPC or UFS cause. No explicit touch phase was recorded at sendEvent, and the UIKit conversion coefficient was not dynamically read.

After the tap the Home Screen showed edit markers, not Settings content. Window arm/restore passed, overflow/revocation/emission loss were zero, and both captures had no drops/gaps/panic. Both boots returned through the verified RPC/sync path. The user demonstrated Search/Siri Suggestions with physical touch, but full physical touch and Settings are not qualified.

Stock8b8e72da was fully SHA restored; Android/root/green/boot_completed1 confirmed, screen off charging. The owner requests unattended continuation and prioritizes all CPUs after touch, with UFS comparison against sheng. Complete CPU configuration/header rebuild and recovery qualification are required before unattended SMP. Full peripheral Goal remains active. See paired workbench research/HANDOVER-UI-DELIVERY-DELAY-20261111.md for hashes and raw evidence.
