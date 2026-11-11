"""Bind the policy-log profile to exact diagnostics-only source and codegen."""
import hashlib,json
def verify(root,version,d):
 sha=lambda x:hashlib.sha256(x).hexdigest();q=json.loads((d/'ulog-qualification.json').read_text())
 assert q['mode']==3 and q['control_set'] and q['only_diagnostic_set'] and not q['power_controls_added']
 assert q['filter_checks']>=163 and q['boot_request_cap']==12 and q['set_request_cap']==2
 assert q['diagnostic_mask']=='0x402000020' and q['level']==3 and q['restore_mask']==0 and q['restore_level']==4
 proof_path=root/'research/pd-policy-20261110/filter-protocol-proof.json';assert sha(proof_path.read_bytes())==q['protocol_proof_sha256']
 proof=json.loads(proof_path.read_text());assert proof['accepted'] and proof['cold_level']==4 and proof['cold_override_mask']==0 and not proof['power_control']
 c=root/f'edl_work/basic-bundle-20261108/v{version}/ulog-filter-codegen/audit.json';assert sha(c.read_bytes())==q['native_codegen_sha256'];audit=json.loads(c.read_text())
 b=json.loads((d.parent/'archives/EldenPmicClient/build.json').read_text());assert b['archive_sha256']==audit['archive_sha256'] and audit['accepted'] and audit['shutdown_hook']
 for n,record in audit['objects'].items():assert record['archive_member_equal'] and not record['fp_simd'] and record['sha256']==b['objects'][n]
 source=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 for n,h in q['sources'].items():assert sha((source/'power'/n).read_bytes())==h,n
 assert sha((source/'usb/EldenSdamRecovery.inc').read_bytes())==q['recovery_source_sha256']
 return q
