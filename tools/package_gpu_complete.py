"""Archive verified offline GPU artifacts and an inactive userland overlay.

No tablet, OS image or boot partition is read or modified. The optional publish
marker is kept outside the rootfs overlay, so staging it does not enable Metal.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, tarfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    report = json.loads((build/'result.json').read_text())
    if not report.get('complete_offline_build') or not report['gpu_specific_abi']['gpu_specific_abi_checks_passed']:
        raise ValueError('Complete build and GPU-specific ABI evidence required')
    for rel, row in report['artifacts'].items():
        p = Path(rel)
        if p.is_absolute() or '..' in p.parts or sha(build/p) != row['sha256']:
            raise ValueError('Artifact identity changed: ' + rel)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    payload = out/'payload'
    payload.mkdir()
    def copy(src, rel):
        target = payload/rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)
    for rel in ('entry/kernel.macho', 'linked/native.macho', 'linked/libEldenGpu.a',
                'tree/build/adreno-metal-bridge/ios/libadreno_metal_bridge.a'):
        copy(build/rel, 'native/'+Path(rel).name)
    user = 'rootfs/private/var/ngpu'
    shutil.copytree(build/'metal/NativeAdrenoMetal.bundle', payload/user/'addons/NativeAdrenoMetal.bundle')
    copy(build/'metal/adrenoprobed', user+'/bin/adrenoprobed')
    copy(build/'metal/org.blu-sharky.adrenoprobed.plist', user+'/daemons/org.blu-sharky.adrenoprobed.plist')
    (payload/user/'tu_debug').write_text('sysmem,nocb\n')
    activation = payload/'activation'
    activation.mkdir()
    (activation/'NativeAdrenoMetal.publish').write_text('1\n')
    (activation/'kernel-args.fragment').write_text('elden-gpu=1 elden-gpu-hz=<reviewed-Y700-OPP>\n')
    metal = json.loads((build/'metal/result.json').read_text())
    (activation/'trust-cdhashes.json').write_text(json.dumps(metal['cdhashes'], indent=2)+'\n')
    for src in (build/'result.json', build/'gpu-abi.json', build/'stages.json',
                build/'entry/result.json', build/'recovery/result.json',
                build/'frame-test/result.json', build/'runtime-test/result.json', build/'metal/result.json'):
        copy(src, 'reports/'+src.parent.name+'-'+src.name)
    for name in ('GPU-MERGE.md', 'GPU-BUILD.md'):
        copy(ROOT/'docs'/name, 'docs/'+name)
    # Modes survive packaging; Windows extraction is not installation.
    for p in payload.rglob('*'):
        if p.is_file():
            p.chmod(0o755 if p.name in ('NativeAdrenoMetal', 'adrenoprobed', 'libvulkan_freedreno.dylib') else 0o644)
    manifest = {p.relative_to(payload).as_posix(): {'sha256': sha(p), 'bytes': p.stat().st_size}
                for p in sorted(payload.rglob('*')) if p.is_file()}
    (payload/'manifest.json').write_text(json.dumps({'files': manifest, 'default_enabled': False,
        'boot_packaged': False, 'hardware_qualified': False,
        'note': 'Offline merge inputs only; activation requires a reviewed boot/menu/trust/Data integration.'}, indent=2)+'\n')
    archive = out/'elden-a830-offline.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(payload, arcname='elden-a830-offline')
    # Read every stored regular file back; compare archive contents with manifest.
    with tarfile.open(archive, 'r:gz') as tar:
        for rel, row in manifest.items():
            data = tar.extractfile('elden-a830-offline/'+rel).read()
            if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('Archive readback mismatch: '+rel)
    (out/'result.json').write_text(json.dumps({'passed': True, 'archive_sha256': sha(archive),
        'bytes': archive.stat().st_size, 'files': len(manifest), 'archive_readback_passed': True,
        'device_access': False, 'default_enabled': False}, indent=2)+'\n')
    print('PASS: hashed offline package; activation remains separate')

if __name__ == '__main__':
    main()
