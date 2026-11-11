# OS root mount and Data follow-up

2026-11-07 device evidence: exact OS volume UUID matched via standard chosen/root-matching. BSD root disk3s1 and APFS mount-complete observed. The accepted kernel and authblob are unchanged. Next failure is scratch Data ENOENT; new source diagnostics inspect both mountpoint and device pathname, and expose USB boot argument admission.

Data, PID1 and sustained USB CDC logging remain unverified. Full photo and packaging evidence are in the sibling Y700-Elden research/ROOTMATCH-DATA-LIVE-20261107.md. No GPT/root-image write or QEMU run in this iteration.
