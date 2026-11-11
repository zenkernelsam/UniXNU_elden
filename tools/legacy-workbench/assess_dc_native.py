"""Classify the observed v61 abort without turning AP ownership into permission."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parents[1];run=r/'edl_work/live-dc-native-v61-20261111'
meta=next((run/'usb').glob('*.json'));m=json.loads(meta.read_text());raw=r/m['raw_log'];b=raw.read_bytes();s=b.decode(errors='replace')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(raw)==m['raw_sha256'] and len(b)==m['bytes']
assert m['candidate_session']=='c03b5bb6f24b7cc4' and not m['control_events']
assert 'ELDEN_PD_BOOT phase=6 error=0' in s and 'restored=1' in s
assert 'ELDEN_DC_NATIVE snapshot phase=2 value=10 cycle=1' in s
assert 'open=0 path-apid=702 usb-apid=704 dc-apid=705 dc-owner=0' in s
assert 'ELDEN_DC_NATIVE phase=3 error=0 attempts=0 completions=0' in s
assert 'ELDEN_DC_NATIVE write raw=' not in s
assert 'pc:  0xfffffff0079ff768' in s and 'esr: 0x96000010' in s
events=m['recovery_events'];wait=next(x for x in events if 'PANIC_WAIT' in x['line']);reset=next(x for x in events if 'PSCI_RESET' in x['line']);delta=reset['elapsed']-wait['elapsed'];assert 178<delta<185
restore=r/'edl_work/dc-native61-stock-20261111/flash/result.json';q=json.loads(restore.read_text());assert q['success'] and q['full_readback_sha256']=='8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890'
drops=[int(x) for x in re.findall(r' dropped=(\d+)',s)];assert drops and max(drops)==0
report=dict(result='native-write-failed-before-command-submit',functional_acceptance=False,repeat_candidate=False,
 image_sha256='8dd8bc731d177869a2ff62459a7714b06e064992f338e3b07e0577c6a796c8c9',
 raw_sha256=sha(raw),bytes=len(b),max_reported_dropped=max(drops),host_discontinuities=m['discontinuities'],
 pd_bootstrap_passed=True,glink_pre_read_passed=True,native_open_crosscheck_passed=True,
 lower_write_completed=False,lower_readback_observed=False,restore_on_stop_tested=False,
 pc='0xfffffff0079ff768',instruction='str w3,[x8]',esr='0x96000010',dfsc='0x10 synchronous external abort',
 writer_apid=705,data_register_pa_from_source=hex(0xc500000+705*0x1000+0x10),data_value=9,
 cause='CPU store to writer FIFO window aborts; exact access restriction remains unproven. Not an APID/ACL bypass authorization.',
 panic_wait_to_reset_seconds=delta,stock_restore_path=restore.relative_to(r).as_posix(),stock_restore_sha256=sha(restore),
 android_final_path='edl_work/dc-native61-stock-20261111/android-final.txt',
 android_final_sha256=sha(r/'edl_work/dc-native61-stock-20261111/android-final.txt'))
p=r/'research/dc-native-20261111/live-assessment.json';p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
