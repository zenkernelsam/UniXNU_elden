"""Compile the existing raw-entry observer for an owned kernel fixture in WSL."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--port', type=Path, required=True)
a = p.parse_args()
f = a.fixture.resolve()
state = json.loads((f / 'entry.json').read_text())
recipe = [
    ['clang', '--target=aarch64-none-elf', '-c', str(f / 'entry.S'), '-o', str(f / 'entry.o'),
     '-DBOOT_ARGS_PA=' + state['boot_args_pa'], '-DKERNEL_ENTRY_PA=' + state['entry_pa']],
    ['ld.lld', '-T', str(f / 'link.ld'), str(f / 'entry.o'), '-o', str(f / 'entry.elf')],
]
for command in recipe:
    subprocess.run(command, check=True)
spec = importlib.util.spec_from_file_location('shim_extract', a.port / 'scripts/build_boot_shim.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
blob = module.extract_alloc_sections((f / 'entry.elf').read_bytes())
(f / 'entry.bin').write_bytes(blob)
(f / 'entry-compile.json').write_text(json.dumps(recipe, indent=2) + '\n')
print('compiled raw-entry helper:', len(blob), 'bytes; kernel entry', state['entry_pa'])
