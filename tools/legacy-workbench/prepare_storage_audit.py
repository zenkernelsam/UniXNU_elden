"""Mac: prepare an owned-candidate storage ABI audit without changing shared sources."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys
import os

root=Path.cwd()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate',type=Path,default=root/'out/bringup-20261105/kernel-gop.macho')
parser.add_argument('--plan',type=Path,default=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_probe/kernel/kernel.json')
parser.add_argument('--work',type=Path,default=root/'out/elden-storage-abi')
parser.add_argument('--tree',type=Path,help='compiled source tree for a diagnostic derivative plan')
inputs=parser.parse_args()
tree=inputs.tree.resolve() if inputs.tree else inputs.plan.resolve().parents[1]/'tree'
work=inputs.work.resolve()
(work/'scripts').mkdir(parents=True,exist_ok=True)
(work/'build').mkdir(exist_ok=True)
(work/'refer').mkdir(exist_ok=True)
(work/'kits').mkdir(exist_ok=True)
for target,source in ((work/'build/xnu',tree/'build/xnu'),(work/'.cache',tree/'.cache'),
                      (work/'refer/IOStorageFamily',tree/'refer/IOStorageFamily'),
                      (work/'kits/XnuPrelink',tree/'kits/XnuPrelink')):
    if not target.exists(): target.symlink_to(source,target_is_directory=True)
code=(root/'third_party/UniXNU/scripts/audit-storage-interface.py').read_text()
code=code.replace('import hashlib','import argparse\nimport hashlib',1)
needle='ROOT = Path(__file__).resolve().parents[1]'
code=code.replace(needle,needle+'''
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate',type=Path,required=True)
parser.add_argument('--plan',type=Path,required=True)
parser.add_argument('--snapshot',type=Path)
parser.add_argument('--static-only',action='store_true')
args=parser.parse_args()
''',1)
code=code.replace("kernel_path = ROOT / 'build/xnu-prelink/native-apfs-load-positive.macho'","kernel_path = args.candidate")
code=code.replace("plan = json.loads(kernel_path.with_suffix('.json').read_text())","plan = json.loads(args.plan.read_text())")
code=code.replace("command += ['-I', str(out / 'include'),", "command[1:1] = ['-I', str(ROOT / '.cache/xnu-work/osfmk')]\ncommand += ['-I', str(out / 'include'),",1)
# The concrete kernel also exports the binder's genuine original-class
# aliases. They must not inflate the invariant count of source-kernel slots.
needle="if all(pointers[i] == by_name[name] for i, name in enumerate(methods) if name in by_name):"
assert needle in code
code=code.replace(needle,"if all(pointers[i] == by_name[name] for i, name in enumerate(methods) if name in by_name and 'IOBlockStorageDevice' not in name):",1)
code=code.replace("matched = sum(name in by_name for name in methods)",
    "matched = sum(name in by_name and 'IOBlockStorageDevice' not in name for name in methods)",1)
code=code.replace("if name not in by_name:\n        require(text['va'] <= pointers[i] < text['va'] + text['size'], 'Owned storage method outside original text')",
    "if name not in by_name or 'IOBlockStorageDevice' in name:\n        require(text['va'] <= pointers[i] < text['va'] + text['size'], 'Owned storage method outside original text')\n        if name in by_name:\n            require(by_name[name]==pointers[i], 'Original virtual-method alias disagrees with dispatch')",1)
needle="d = json.loads((ROOT / 'build/darwinboot-storage-native-metadata/gdb-stop.json').read_text())"
assert needle in code
static='''
static_record=dict(candidate_sha256=plan['candidate_sha256'],original_sha256=plan['original_sha256'],
    public_source_revision=revision,method_slots=190,vtable=hex(table),original_metaclass=hex(meta),
    source_ioservice_metaclass=hex(service_meta),original_protected_constructor=hex(constructors[0][0]),
    original_base_destructor=hex(destructor),abstract_complete_destructors_trap=True,
    osmeta_ctor={name:hex(value) for name,value in by_name.items() if 'OSMetaClassC2' in name},
    storage_ready_function=hex(by_name['_elden_ufs_storage_class_ready']) if '_elden_ufs_storage_class_ready' in by_name else None,
    concrete_ufs_metaclass=hex(by_name['__ZN14NativeUfsBlock10gMetaClassE']) if '__ZN14NativeUfsBlock10gMetaClassE' in by_name else None,
    methods=[dict(slot=i,reference_name=name,candidate_target=hex(pointers[i]),kernel_names=exports.get(pointers[i],[])) for i,name in enumerate(methods)],
    static_original_code_unchanged=True,live_native_4k=False)
(out/'static-audit.json').write_text(json.dumps(static_record,indent=2)+'\\n')
if args.static_only:
    print('PASS: original storage static ABI; live native 4K not yet qualified')
    raise SystemExit(0)
d=json.loads(args.snapshot.read_text())
'''
code=code.replace(needle,static,1)
needle="service_meta = by_name['__ZN9IOService10gMetaClassE']"
replacement=needle+'''
for symbol,target in {
    '__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass':constructors[0][0],
    '__ZN20IOBlockStorageDeviceD2Ev':destructor,
    '__ZN20IOBlockStorageDevice10gMetaClassE':meta,
    '__ZTV20IOBlockStorageDevice':table,
}.items():
    if symbol in by_name:
        require(by_name[symbol]==target,'Original base alias disagrees with independently qualified address: '+symbol)
'''
assert needle in code
code=code.replace(needle,replacement,1)
# Read actual compiled OSMetaClass member offsets instead of importing an unrelated AES audit.
needle="layout = json.loads((ROOT / 'build/image4-ignition/aes-interface-audit.json').read_text())['compiled_metaclass_offsets']"
replacement='''
meta_dwarf=subprocess.check_output(['xcrun','dwarfdump','--name=OSMetaClass','--show-children',obj_path],text=True)
layout={}
for member in ('superClassLink','className','classSize'):
    found=re.search(r'DW_AT_name\\s+\\("'+member+r'"\\)[\\s\\S]{0,600}?DW_AT_data_member_location\\s+\\((0x[0-9a-f]+|[0-9]+)\\)',meta_dwarf)
    require(found,'Missing actual metaclass DWARF member '+member)
    layout[member]=int(found[1],0)
'''
assert needle in code
code=code.replace(needle,replacement,1)
needle="r = {k: int(v, 16) for k, v in d['system_registers'].items()}"
replacement='''
if '_elden_ufs_storage_class_ready' in by_name:
    lifecycle=d.get('storage_lifecycle_observation',{})
    require(lifecycle.get('function_return')==1 and lifecycle.get('stopped_before_ufs_mmio') is True,
            'Concrete UFS candidate needs its actual original-class registration gate observed before UFS')
    concrete_meta=by_name['__ZN14NativeUfsBlock10gMetaClassE']
    concrete=bytes.fromhex(d['memory'][f'{concrete_meta:x}:50'])
    require(struct.unpack_from('<Q',concrete,16)[0]==meta,
            'Live concrete UFS class does not inherit the qualified original block class')
r = {k: int(v, 16) for k, v in d['system_registers'].items()}
'''
assert needle in code
code=code.replace(needle,replacement,1)
needle="r = {k: int(v, 16) for k, v in d['system_registers'].items()}"
replacement='''
observed=d.get('sctlr_instruction_observation',{})
require(observed.get('instruction')=='MSR SCTLR_EL1, x0' and
        observed.get('encoding')=='0xd5181000' and
        observed.get('input_x0')==observed.get('rsp_after')==d['system_registers'].get('SCTLR_EL1') and
        observed.get('rsp_description',{}).get('bitsize')=='64' and
        observed.get('guest_code_modified') is False,
        'SCTLR_EL1 must be observed against the original kernel instruction, not assumed from a register alias')
r = {k: int(v, 16) for k, v in d['system_registers'].items()}
'''
assert needle in code
code=code.replace(needle,replacement,1)
needle="(out / 'interface-audit.json').write_text"
code=code.replace(needle,"result['sctlr_instruction_observation']=observed\nresult['raw_entry_fixture']=d.get('raw_entry_fixture',False)\nresult['UEFI_handoff_tested']=d.get('UEFI_handoff_tested',False)\nresult['storage_lifecycle_observation']=d.get('storage_lifecycle_observation')\n"+needle,1)
(work/'scripts/audit-elden-storage.py').write_text(code)
env=dict(os.environ);env['CCC_OVERRIDE_OPTIONS']='+-Wno-unknown-warning-option'
command=[sys.executable,work/'scripts/audit-elden-storage.py','--candidate',inputs.candidate.resolve(),
         '--plan',inputs.plan.resolve(),'--static-only']
result=subprocess.run(command,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(work/'build/static-audit.log').write_text(result.stdout)
print(result.stdout[-6500:])
raise SystemExit(result.returncode)
