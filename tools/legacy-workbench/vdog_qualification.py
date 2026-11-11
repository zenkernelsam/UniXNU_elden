import hashlib,json
def verify(root,version,kernel):
 assert version in (78,79,80)
 path=root/f'edl_work/basic-bundle-20261108/v{version}/vdog-codegen.json';p=json.loads(path.read_text())
 assert p['accepted'] and p['original_controls_unchanged'] and not p['smp_enabled']
 if version==78:assert p['no_pet_trial']
 else:assert p['initial_pet']==1 and p['periodic_pet'] is False
 if version>=80:assert p['bark_ms']==27000
 assert p['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 src=root/f'research/basic-bundle-20261108/source-compiled-v{version}/usb/EldenSdamRecovery.inc'
 assert p['recovery_sha256']==hashlib.sha256(src.read_bytes()).hexdigest()
 return {'codegen_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'seconds':30,'initial_active_refused':True,'qualification_only':True}
