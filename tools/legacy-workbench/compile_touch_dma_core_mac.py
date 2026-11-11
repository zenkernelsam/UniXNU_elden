"""Compile the reviewed touch DMA wrapper separately; no runtime installation."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path

ROOT = Path('/Users/ciscohe/Desktop/UniXNU_elden')
assert Path.cwd().resolve() == ROOT
SOURCE = ROOT / 'research/touch-dma-core-20261109/source-reviewed-v1'
OUT = ROOT / 'out/touch-dma-core-20261109-v1'
LLVM = Path('/Users/ciscohe/toolchains/llvm-20/bin')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, check=True)
    return result.stdout.decode()


manifest = json.loads((SOURCE / 'source-manifest.json').read_text())
for name, expected in manifest['files'].items():
    assert sha(SOURCE / name) == expected, name
assert manifest['files']['GeniSpi.c'] == '2042cfc9647391e24d5a558202046425ac62a05adfc73afe769367b8374e3f91'
assert manifest['files']['GeniSpi.h'] == 'e73891e4571c5323bca27aaf987bafa002856dfa1dcee156ff31b119e2d3ae3c'
assert sha(Path(__file__)) == manifest['build_script_sha256']
OUT.mkdir(exist_ok=False)
context_path = ROOT / 'out/adsp-pmic-ucsi45-20261108/EldenPmicClient/build.json'
context = json.loads(context_path.read_text())
assert context['arm64_ios17']
template = next(c for c in context['commands'] if any(x.endswith('/ChargePoll.c') for x in c))
assert template[template.index('-target') + 1] == 'arm64-apple-ios17.0'
commands, objects, audits = [], [], {}
for name in ('GeniSpi.c', 'DmaTransfer.c'):
    command = list(template)
    inputs = [i for i, x in enumerate(command) if x.endswith('.c') and not x.startswith('-')]
    assert len(inputs) == 1
    obj = OUT / (Path(name).stem + '.o')
    command[inputs[0]] = str(SOURCE / name)
    command[command.index('-o') + 1] = str(obj)
    # Use only this frozen header set, not the original PMIC source directory.
    command[command.index('-I') + 1] = str(SOURCE)
    run(command)
    commands.append(command)
    raw = obj.read_bytes()
    magic, cpu, _, filetype, count, _, _, _ = struct.unpack_from('<8I', raw)
    assert magic == 0xfeedfacf and cpu == 0x100000c and filetype == 1
    at, target_ok, sections = 32, False, []
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', raw, at)
        if cmd == 0x32:
            platform, minimum = struct.unpack_from('<II', raw, at + 8)
            assert platform == 2 and minimum == 17 << 16
            target_ok = True
        if cmd == 0x19:
            nsects = struct.unpack_from('<I', raw, at + 64)[0]
            for j in range(nsects):
                section = at + 72 + j * 80
                label = raw[section:section + 16].split(b'\0')[0].decode()
                length = struct.unpack_from('<Q', raw, section + 40)[0]
                assert label != '__mod_init_func' or length == 0
                sections.append(label)
        at += size
    assert target_ok
    audits[obj.name] = dict(sha256=sha(obj), sections=sections, arm64_ios17=True)
    objects.append(obj)
archive = OUT / 'libEldenTouchDma.a'
archive_command = [LLVM / 'llvm-ar', 'rcsD', archive, *objects]
run(archive_command)
nm = run([LLVM / 'llvm-nm', '-u', archive])
assert '_memcpy' in nm and '_geni_spi_transfer' in nm
(OUT / 'undefined-symbols.txt').write_text(nm)
report = dict(archive_sha256=sha(archive), archive_bytes=archive.stat().st_size,
              source_manifest_sha256=sha(SOURCE / 'source-manifest.json'),
              inherited_context_sha256=sha(context_path), commands=commands,
              archive_command=[str(x) for x in archive_command], objects=audits,
              automatic_initializers=False, runtime_linked=False, hardware_tested=False,
              source_files=manifest['files'])
(OUT / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({key: report[key] for key in ('archive_sha256', 'archive_bytes', 'runtime_linked', 'hardware_tested')}, indent=2))
