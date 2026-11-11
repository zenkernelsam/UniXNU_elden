"""Bind native DC trial scope to the reviewed frozen sources and real objects."""
import hashlib,json

def verify(root,version,d):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 q=json.loads((d/'dc-native-qualification.json').read_text())
 assert q['accepted'] and q['hardware_tested'] is False
 assert q['checks']==1432 and q['spmi_checks']==50036
 assert q['target']=={'bus':0,'sid':7,'address':'0x2a52','raw_values':[9,10],'maximum_ma':500,'low_ma':450,'max_writes':2}
 assert q['real_spmi_completion'] and q['stop_writer_lock'] and not q['higher_current_supported']
 s=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 for n,h in q['frozen_sources'].items():assert sha(s/n)==h,n
 p=root/q['codegen_path'];assert sha(p)==q['codegen_sha256'];a=json.loads(p.read_text())
 assert a['accepted'] and a['stop_writer_lock'] and a['no_synthetic_glink_ack']
 b=json.loads((d.parent/'archives/EldenPmicClient/build.json').read_text());assert a['archive_sha256']==b['archive_sha256']
 for n,v in a['objects'].items():assert v['archive_member_equal'] and not v['fp_simd'] and v['sha256']==b['objects'][n]
 for n,h in a['sources'].items():assert sha(s/'power'/n)==h,n
 review=root/q['source_review_path'];assert sha(review)==q['source_review_sha256']
 recovery=(s/'usb/EldenSdamRecovery.inc').read_text()
 assert recovery.index('elden_dc_native_request_stop();')<recovery.index('elden_backlight_request_stop();')<recovery.index('elden_pd_bootstrap_request_stop();')
 return q
