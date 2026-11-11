"""Mac: build the audited read-only UFS derivative with the accepted FD.

Only the entry band and the existing diagnostic AMFI latch workaround are
added to the completed source kernel. No firmware rebuild or device I/O.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import sys

root = Path.cwd()
base = root / 'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_rootcheck/kernel/kernel.macho'
plan_path = base.with_suffix('.json')
adt = root / 'out/gop-entry-20261106/probe/kernel.macho.adt'
work = root / 'out/rootcheck-entry-20261106'
work.mkdir(parents=True, exist_ok=True)
expected = '07d4df1d49e0dd64acab02e84a74d452c7034acd92e5a44b0ce37d67f53e4b15'
assert hashlib.sha256(base.read_bytes()).hexdigest() == expected
assert hashlib.sha256(adt.read_bytes()).hexdigest() == 'b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3'
audit = json.loads((root / 'out/elden-storage-abi-rootcheck/build/storage-abi/interface-audit.json').read_text())
assert audit['candidate_sha256'] == expected and audit['live_native_4k']
assert audit['storage_lifecycle_observation']['function_return'] == 1
sys.path.insert(0, str(root / 'scripts'))
import make_entry_probe_kernel as maker
import struct
data = base.read_bytes()
offset = maker.locate(data, 0xfffffff004aaecfc)[1]
assert struct.unpack_from('<I', data, offset)[0] == 0xaa1503e0
subprocess.run([sys.executable, root / 'scripts/make_entry_probe_kernel.py',
    '--kernel', base, '--adt', adt, '--expect-kernel-bytes', str(len(data)),
    '--expect-kernel-sha256', expected, '--output-dir', work,
    '--clang', '/usr/bin/clang', '--poke', '0xfffffff004aaecfc=0x17ffffd4'], check=True)
facts = json.loads((work / 'probe/probe.json').read_text())
assert facts['layout']['poke'][0]['word'] == 0xaa1503e0
derivative = work / 'probe/kernel.macho'
plan = json.loads(plan_path.read_text())
plan['candidate_sha256'] = hashlib.sha256(derivative.read_bytes()).hexdigest()
plan['diagnostic_derivative'] = dict(baseline_sha256=expected,
    baseline_plan_sha256=hashlib.sha256(plan_path.read_bytes()).hexdigest(),
    entry_probe=facts['layout'], real_power_manager_implemented=False,
    hardware_tested=False, storage_io_tested=False)
(work / 'probe/kernel.json').write_text(json.dumps(plan, indent=2) + '\n')
subprocess.run([sys.executable, root / 'out/package-candidate.py', '--kernel', derivative,
    '--adt', derivative.with_suffix('.macho.adt'), '--work', work,
    '--config', root / 'package/elden-ufs-rootcheck-unixnu.toml',
    '--xnu-title', 'ELDEN UFS READ CHECK - LUN4'], check=True)
print('Prepared read-only entry packet:', derivative, plan['candidate_sha256'])
