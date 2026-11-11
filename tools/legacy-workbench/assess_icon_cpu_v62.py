"""Record observed v62 milestones and the unexercised renderer boundary."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parents[1];run=r/'edl_work/live-icon-cpu-v62-20261111'
meta=next((run/'usb').glob('*.json'));m=json.loads(meta.read_text());p=r/m['raw_log'];b=p.read_bytes();s=b.decode(errors='replace');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(p)==m['raw_sha256'] and len(b)==m['bytes'] and m['candidate_session']=='38c8694ef90aa5dc'
assert not m['discontinuities'] and max(map(int,re.findall(r'dropped=(\d+)',s)))==0
assert 'ELDEN_ICON_CPU stage=ready error=0 bytes=69456 sha=3014490c' in s
assert 'ELDEN_UI_REPLAY swipe-up sent=25 error=0 release=0 source=software' in s
assert 'ELDEN_UI_REPLAY settings down=0 up=0' in s
assert 'ELDEN_RECOVERY DEADLINE_FIRED' in s and 'ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in s
assert 'ELDEN_ICON_CPU environment appended' not in s and 'ELDEN_ICON_CPU installed' not in s
frames=json.loads((run/'frames/report.json').read_text());assert len(frames)==10 and all(x['all_rows_verified'] for x in frames)
restore=r/'edl_work/icon-cpu62-stock-20261111/flash/result.json';a=json.loads(restore.read_text());assert a['success'] and a['full_readback_sha256']=='8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890'
report=dict(image_sha256='0aadf0cbc3c2ea0e7120f59e6616b54c86d8478ce97675ee5b12cb53acbc9f37',kernel_sha256='a2900c0bf091fff75cdf31de7e69a2325549b8f4efd3f2f176170eab59a7ae07',
 raw_sha256=sha(p),bytes=len(b),max_reported_dropped=0,elapsed_seconds=m['elapsed_seconds'],ram_install_verified=True,
 environment_append_observed=False,library_install_observed=False,renderer_tested=False,
 software_swipe_completed=True,desktop_frame=7,settings_report_returned_zero=True,
 preferences_observed_in_samples='name=Preferences' in s,settings_content_verified=False,
 frame_budget_exhausted=True,hardware_touch_tested=False,return_method='900-second local deadline',
 stock_restore_sha256=sha(restore),android_final_sha256=sha(r/'edl_work/icon-cpu62-stock-20261111/android-final.txt'),
 interpretation='Transport/report success is not app input acceptance; no renderer result without environment/install evidence. Absence from periodic process samples does not prove a process never existed.')
out=r/'research/icon-cpu-live-v62-20261111/live-assessment.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
