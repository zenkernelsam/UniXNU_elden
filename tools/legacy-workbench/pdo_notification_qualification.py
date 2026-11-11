"""Verify the board-specific PDO notification quirk independently of packaging."""
import hashlib,json
def verify(root,version,d):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 q=json.loads((d/'ucsi-events-qualification.json').read_text());assert q['accepted'] and q['checks']>=431 and q['pdo_notification_quirk']
 assert q['notification_control']=='0xda650005'
 review=(root/q.get('pdo_notification_review_path','research/pdo-events-20261110/source-review.json')).resolve()
 assert review.is_relative_to((root/'research').resolve()) and sha(review)==q['pdo_notification_review_sha256'];r=json.loads(review.read_text())
 assert r['new_control']=='0xda650005' and not r['new_voltage_or_current_commands'] and r['raw_features_preserved']
 s=root/f'research/basic-bundle-20261108/source-compiled-v{version}/power'
 for name,h in r['sources'].items():assert sha(s/name)==h,name
 c=root/f'edl_work/basic-bundle-20261108/v{version}/ucsi-codegen/audit.json';assert sha(c)==q['native_codegen_sha256'];code=json.loads(c.read_text())
 assert code['accepted'] and code['pdo_quirk_notifications']
 build=json.loads((d.parent/'archives/EldenPmicClient/build.json').read_text());assert build['archive_sha256']==code['archive_sha256']
 return q
