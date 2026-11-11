"""Fail closed on any DC same-value candidate outside the reviewed fixed scope."""
import hashlib,json
def verify(root,version,directory):
 sha=lambda b:hashlib.sha256(b).hexdigest()
 p=directory/'dc-noop-qualification.json';q=json.loads(p.read_text())
 assert q['accepted'] and q['checks']>=1994 and q['legacy_read_checks']>=932
 assert (q['fixed_bus'],q['fixed_sid'],q['fixed_register'],q['only_write_raw'],q['only_write_ma'])==(0,7,'0x2a52',10,500)
 assert q['max_write_attempts']==1 and q['readback_required'] and not q['higher_current_supported']
 assert q['thermal_token_ms']==1000 and q['hardware_tested'] is False
 frozen=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 for n,h in q['frozen_sources'].items():assert sha((frozen/n).read_bytes())==h,n
 audit_path=root/q['codegen_path'];assert sha(audit_path.read_bytes())==q['codegen_sha256']
 audit=json.loads(audit_path.read_text());assert audit['accepted'] and audit['shutdown_drain_before_pd']
 build=json.loads((root/f'edl_work/basic-bundle-20261108/v{version}/final/archives/EldenPmicClient/build.json').read_text())
 assert audit['archive_sha256']==build['archive_sha256']
 for n,v in audit['objects'].items():assert v['archive_member_equal'] and not v['fp_simd'] and v['sha256']==build['objects'][n]
 for n,h in audit['sources'].items():assert sha((frozen/'power'/n).read_bytes())==h,n
 source=(frozen/'power/DcNoop.c').read_text()
 assert 'put(out+20,0x2a52);put(out+24,10);' in source
 assert 's->write_attempted=true' in source and 'EldenDcNoopVerify' in source
 recovery=(frozen/'usb/EldenSdamRecovery.inc').read_text()
 # Use call sites, not the declarations.
 assert recovery.index('elden_dc_noop_request_stop();')<recovery.index('elden_backlight_request_stop();',recovery.index('static void eldenRecoveryShutdown'))
 return q
