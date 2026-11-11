"""Mac: run the pinned upstream porting validator against current device changes."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import tomllib

root=Path.cwd()
validator=root/'out/offline-20261105/upstream-porting-record.py'
spec=importlib.util.spec_from_file_location('porting_record',validator)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.ROOT=root/'third_party/UniXNU'
record=module.check(root)
old=tomllib.loads(subprocess.check_output(['git','show','HEAD:porting.toml'],cwd=root,text=True))
changed=[
 'scripts/build-elden-ufs-bindings.py','scripts/build-sheng-ufs-probe.py','scripts/build-payload-loader.py',
 'scripts/driver-patches/sheng-ufs-elden-rootcheck.patch',
 'scripts/driver-patches/sheng-ufs-elden-storage-lifecycle.patch','scripts/driver-patches/EldenUfsIdentity.h',
 'package/elden-ufs-rootcheck-unixnu.toml',
 'drivers/EldenPayloadLoader/EldenPayloadLoader.c','drivers/EldenPayloadLoader/EldenPayloadLoader.inf',
 'drivers/EldenPayloadLoader/EldenPayloadLoader.dsc','drivers/EldenPayloadLoader/EldenLoaderTest.c',
 'drivers/EldenPayloadLoader/EldenLoaderTest.inf']
changed += ['scripts/build_boot_shim.py','scripts/build_phase2_image.py','scripts/verify_phase2_artifact.py',
 'patches/overlays/mu_wide_platforms/BootShim/BootShim.S',
 'drivers/EldenPayloadLoader/EldenFdtHandoff.c','drivers/EldenPayloadLoader/EldenFdtHandoff.h',
 'drivers/EldenPayloadLoader/EldenInitrdConsumer.c','drivers/EldenPayloadLoader/EldenInitrdConsumer.inf',
 'drivers/EldenPayloadLoader/EldenRecoveryTest.c','drivers/EldenPayloadLoader/EldenRecoveryTest.inf',
 'drivers/EldenPayloadLoader/EldenFdtMailboxTest.c','drivers/EldenPayloadLoader/EldenFdtMailboxTest.inf']
changed += ['scripts/build-ufs-stage2.py', 'docs/UFS-STAGE2-20261106.md']
changed += [str(p.relative_to(root)).replace('\\', '/') for p in sorted((root/'drivers/EldenUfsStage2').rglob('*'))
            if p.is_file() and 'Generated' not in p.parts]
changed += ['docs/PREREQUISITES-20261106.md', 'drivers/EldenPayloadLoader/EldenBootProperties.c', 'drivers/EldenPayloadLoader/EldenBootProperties.h', 'drivers/EldenPayloadLoader/EldenBootMetadataTest.c', 'drivers/EldenPayloadLoader/EldenBootMetadataTest.inf', 'drivers/EldenPayloadLoader/Tests/boot_properties_test.c']
module.check_changes({area['id']:area for area in old['area']},
                     {area['id']:area for area in record['area']},changed)
result=dict(areas=len(record['area']),changed_file_ownership_passed=changed,
    validator_sha256=hashlib.sha256(validator.read_bytes()).hexdigest(),
    catalog_sha256=hashlib.sha256((module.ROOT/'components.toml').read_bytes()).hexdigest())
(root/'out/payload-loader-20261106/porting-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: current structured porting record and ownership for',len(changed),'changed files')
