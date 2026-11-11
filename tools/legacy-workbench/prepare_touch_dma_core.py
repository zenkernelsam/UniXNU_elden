"""Freeze offline-only T1 DMA inputs. Never modify existing candidates."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE = ROOT.parents[1] / 'UniXNU_elden'
OUT = ROOT / 'research/touch-dma-core-20261109/source-reviewed-v1'
OUT.mkdir(parents=True, exist_ok=False)
sources = {name: DEVICE / 'drivers/EldenNvt36536' / name
           for name in ('DmaTransfer.h', 'DmaTransfer.c', 'TestDmaTransfer.c')}
sources['EldenTouchResources.h'] = DEVICE / 'soc/sm8750/include/EldenTouchResources.h'
sources.update({name: ROOT / 'research/qcom-geni-spi-20261107' / name
                for name in ('GeniSpi.c', 'GeniSpi.h')})
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for name, path in sources.items():
    shutil.copyfile(path, OUT / name)
script = ROOT / 'edl_work/compile_touch_dma_core_mac.py'
shutil.copyfile(script, OUT.parent / script.name)
manifest = dict(files={name: sha(path) for name, path in sources.items()},
                build_script_sha256=sha(script),
                canonical_revision='b5399b5646c4d7415deec6d887fd51715a97a024',
                scope='offline wrapper only; requires a qualified external SIDa3 owner',
                runtime_linked=False, hardware_tested=False)
(OUT / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
base = '/mnt/d/Desktop/Git/Patch/Y700-Elden'
source = base + '/research/touch-dma-core-20261109/source-reviewed-v1'
binary = base + '/edl_work/touch-dma-core-20261109/test-dma-frozen'
compile_command = ['wsl', '-d', 'Ubuntu-22.04', '-u', 'root', '--', 'gcc', '-std=c11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-I', source, source + '/DmaTransfer.c', source + '/TestDmaTransfer.c', source + '/GeniSpi.c',
                   '-o', binary]
subprocess.run(compile_command, check=True, capture_output=True)
test_command = ['wsl', '-d', 'Ubuntu-22.04', '-u', 'root', '--', binary]
result = subprocess.run(test_command, check=True, capture_output=True, text=True)
test = dict(compile_command=compile_command, test_command=test_command, stdout=result.stdout,
            stderr=result.stderr, exit_code=result.returncode, hardware_tested=False,
            source_manifest_sha256=sha(OUT / 'source-manifest.json'))
(OUT.parent / 'host-test.json').write_text(json.dumps(test, indent=2) + '\n')
shutil.copyfile(Path(__file__), OUT.parent / Path(__file__).name)
print(result.stdout.strip())
print('Frozen:', OUT)
