"""Build the entire Elden A830 offline chain with a source-built fixed baseline.

Requires macOS/Xcode, the explicit clean-build inputs and LLVM tool directory,
Rust1.98 and a private setup_gpu_host.py prefix. Outputs must be new. The base
build may resume only through build_clean's identity/proof checks. No device IO.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shlex, shutil, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[1]
BASE_REV = 'a13e5087a8651b3185a0855eacf94923a21e97eb'
BASE_FLAGS = ['--variant', 'bluetooth', '--bt-exit-stack', '--software-keystore',
              '--bluetool-launch', '--bt-transport-trace', '--bluetool-lifecycle',
              '--bt-registry', '--bt-helper-trace', '--bt-process-lifetime',
              '--bt-address17', '--ufs-wait-trial']

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def read(path):
    return json.loads(path.read_text())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('prepared', 'baseline-source', 'base-build', 'inputs', 'shader-prefix', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--mesa-cache', type=Path)
    parser.add_argument('--runtime-cache', type=Path)
    parser.add_argument('--bison', type=Path, help='Bison3.8+; Apple system Bison is too old')
    parser.add_argument('--resume-userspace', action='store_true', help='Resume this same run after native/ABI/Rust succeeded; verify retained inputs and objects')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise ValueError('Full native build requires macOS/Xcode')
    out = args.output.resolve()
    if args.resume_userspace and not out.is_dir():
        raise ValueError('Resume requires the existing complete-build directory')
    out.mkdir(parents=True, exist_ok=args.resume_userspace)
    logs = out/'logs'
    logs.mkdir(exist_ok=args.resume_userspace)
    base = args.base_build.resolve()
    source = args.baseline_source.resolve()
    revision = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip()
    if revision != BASE_REV or subprocess.check_output(['git', '-C', source, 'status', '--porcelain', '--untracked-files=no']):
        raise ValueError('A clean fixed baseline source is required')
    prepared = read(args.prepared/'prepared.json')
    for name, digest in prepared['files'].items():
        rel = Path(name)
        if rel.is_absolute() or '..' in rel.parts or sha(args.prepared/rel) != digest:
            raise ValueError('Prepared input differs: ' + name)
    for key, name in [('source_lock_sha256', 'gpu-build.lock.json'), ('firmware_lock_sha256', 'gpu-firmware.lock.json')]:
        if prepared[key] != hashlib.sha256((ROOT/'config'/name).read_bytes().replace(b'\r\n', b'\n')).hexdigest():
            raise ValueError('Prepared lock differs: ' + name)
    calls = read(out/'stages.json') if args.resume_userspace else []
    completed = {row['stage']: row for row in calls if row['returncode'] == 0}
    if args.resume_userspace:
        for name in ('baseline', 'dependencies', 'derive', 'native-core', 'native-runtime', 'recovery',
                     'frame-test', 'runtime-test', 'CallGate-test', 'Quiesce-test', 'link', 'abi', 'rust'):
            if name not in completed:
                raise ValueError('Required completed stage missing: '+name)
        for rel, row in read(out/'tree/elden-source-manifest.json')['files'].items():
            if sha(out/'tree'/rel) != row['sha256']:
                raise ValueError('Retained source changed: '+rel)
        for name, row in read(out/'core/result.json')['sources'].items():
            if sha(out/'core'/(Path(name).stem+'.o')) != row['object_sha256']:
                raise ValueError('Retained core object changed: '+name)
        for name, row in read(out/'runtime/result.json')['units'].items():
            obj = out/'runtime'/('EldenGpuFirmware.o' if name == 'firmware' else Path(name).stem+'.o')
            if sha(obj) != row['sha256' if name == 'firmware' else 'object_sha256']:
                raise ValueError('Retained runtime object changed: '+name)
        for folder, field, filename in [('recovery','object_sha256','ShengUfs.o'), ('linked','native_sha256','native.macho'), ('linked','kernel_sha256','kernel.macho')]:
            if sha(out/folder/filename) != read(out/folder/'result.json')[field]:
                raise ValueError('Retained linked/recovery artifact changed')
        if read(out/'gpu-abi.json')['kernel_sha256'] != sha(out/'linked/kernel.macho'):
            raise ValueError('Retained ABI audit differs')
        if sha(out/'tree/build/adreno-metal-bridge/Cargo.lock') != sha(ROOT/'config/gpu-cargo.lock'):
            raise ValueError('Retained Cargo lock differs')
        # The failed Meson configuration cached Apple's old Bison path. Preserve
        # that attempt and start only Turnip's build directory afresh.
        if 'turnip' not in completed:
            failed = out/'attempts'/('turnip-'+str(len(list((out/'attempts').glob('*'))) if (out/'attempts').exists() else 0))
            failed.mkdir(parents=True)
            old = out/'tree/build/mesa-tu-ios'
            if old.exists():
                if not old.resolve().is_relative_to(out) or not failed.resolve().is_relative_to(out):
                    raise ValueError('Build archive escaped output')
                shutil.move(str(old), str(failed/'mesa-tu-ios'))
            for p in (out/'tree/.cache').glob('mesa-tu-ios-*.log'):
                shutil.copyfile(p, failed/p.name)
    env = dict(os.environ)
    inputs = read(args.inputs)
    llvm = Path(inputs['llvm'])
    bison = args.bison or next((p for p in [Path('/usr/local/opt/bison/bin/bison'), Path('/opt/homebrew/opt/bison/bin/bison')] if p.is_file()), None)
    if not bison:
        raise ValueError('Supply --bison with Bison3.8+; no system tool is modified')
    version = subprocess.check_output([str(bison), '--version'], text=True).splitlines()[0]
    number = re.search(r'\b(\d+)\.(\d+)(?:\.\d+)?\b', version)
    if not number or tuple(map(int, number.groups())) < (3, 8):
        raise ValueError('Bison3.8+ required: '+version)
    # Only the legacy grammar deprecation is suppressed, as in the qualified
    # prior Turnip build. All parser error/counterexample diagnostics remain.
    env['BISON'] = shlex.join([str(bison.resolve()), '-Wno-deprecated'])
    # Mesa uses find_program('bison'), which does not consult BISON. Supply a
    # private executable wrapper in PATH; never replace the system executable.
    host_tools = out/'host-tools'
    host_tools.mkdir(exist_ok=True)
    wrapper = host_tools/'bison'
    wrapper.write_text('#!'+sys.executable+'\nimport os,sys\nos.execv('+repr(str(bison.resolve()))+', '+
                       repr([str(bison.resolve()), '-Wno-deprecated'])+'+sys.argv[1:])\n')
    wrapper.chmod(0o755)
    env['PATH'] = ':'.join(map(str, [host_tools, args.shader_prefix/'venv/bin', args.shader_prefix/'install/bin',
                                     Path.home()/'.cargo/bin', llvm])) + ':' + env['PATH']
    env['CARGO_BUILD_JOBS'] = '4'
    env['PYTHONPYCACHEPREFIX'] = str(out/'pycache')
    def run(name, command, cwd=None):
        command = list(map(str, command))
        if args.resume_userspace and name in completed:
            if completed[name]['command'] != command:
                raise ValueError('Completed command changed: '+name)
            print('Verified retained stage ' + name, flush=True)
            return
        row = {'stage': name, 'command': command, 'started': time.time()}
        calls.append(row)
        print('Building ' + name, flush=True)
        log = logs/(name+'.log')
        if log.exists():
            log = logs/(name+'-retry-'+str(len(calls))+'.log')
        with log.open('xb') as stream:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        row.update(returncode=result.returncode, seconds=time.time()-row['started'])
        (out/'stages.json').write_text(json.dumps(calls, indent=2)+'\n')
        if result.returncode:
            raise RuntimeError(name + ' failed; original logs retained in ' + str(logs))
    def tool(name, *arguments):
        return [sys.executable, ROOT/'tools'/name, *arguments]
    run('baseline', [sys.executable, source/'tools/build_clean.py', *BASE_FLAGS,
                     '--inputs', args.inputs.resolve(), '--output', base, '--target', 'kernel', '--jobs', '4',
                     *(['--resume'] if base.exists() else [])], source)
    if not read(base/'build-state.json')['complete']:
        raise ValueError('Baseline incomplete')
    tree = out/'tree'
    if not args.resume_userspace:
        shutil.copytree(args.prepared/'tree', tree)
        shutil.copytree(args.prepared/'opps', out/'opps')
        shutil.copytree(args.prepared/'firmware', out/'firmware')
    extras = []
    for key in ('mesa_cache', 'runtime_cache'):
        if getattr(args, key):
            extras += ['--'+key.replace('_', '-'), getattr(args, key).resolve()]
    run('dependencies', tool('prepare_gpu_dependencies.py', '--tree', tree, *extras))
    # Upstream expects this private venv path; it contains tools, never GPU objects.
    if not args.resume_userspace:
        (tree/'.cache/mesa-venv').symlink_to(args.shader_prefix.resolve()/'venv', target_is_directory=True)
    run('derive', tool('prepare_gpu_runtime.py', '--tree', tree, '--output', out/'derived'))
    run('native-core', tool('build_gpu_native.py', '--tree', tree, '--base-native', base/'native',
                           '--xnu-source', base/'source/xnu', '--submit', out/'derived', '--output', out/'core', '--jobs', '4'))
    run('native-runtime', tool('build_gpu_runtime.py', '--tree', tree, '--derived', out/'derived',
        '--core-build', out/'core', '--base-native', base/'native', '--opps', out/'opps',
        '--firmware', out/'firmware', '--llvm', llvm, '--output', out/'runtime'))
    run('recovery', tool('build_gpu_recovery.py', '--base', base/'ufs-wait-trial', '--output', out/'recovery'))
    run('frame-test', tool('test_gpu_frame.py', '--derived', out/'derived', '--wire', tree/'drivers/GopFramebuffer', '--output', out/'frame-test'))
    run('runtime-test', tool('test_gpu_runtime_control.py', '--derived', out/'derived', '--output', out/'runtime-test'))
    for name in ('CallGate', 'Quiesce'):
        exe = out/('test-'+name)
        run(name+'-compile', ['xcrun', 'clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-pthread', '-I', out/'derived', out/'derived'/('Test'+name+'.cpp'), '-o', exe])
        run(name+'-test', [exe])
    run('link', tool('link_gpu_kernel.py', '--tree', tree, '--base', base/'ufs-wait-trial',
        '--core', out/'core', '--runtime', out/'runtime', '--recovery', out/'recovery', '--llvm', llvm,
        '--original-kernelcache', inputs['original_kernelcache'], '--output', out/'linked'))
    run('abi', tool('audit_gpu_link.py', '--linked', out/'linked', '--prelink-tool', base/'ufs-wait-trial/prelink-tool', '--output', out/'gpu-abi.json'))
    run('rust-manifest', [sys.executable, tree/'scripts/build-adreno-metal-bridge.py', '--target', 'ios', '--manifest-only'])
    cargo_lock = tree/'build/adreno-metal-bridge/Cargo.lock'
    if not args.resume_userspace:
        shutil.copyfile(ROOT/'config/gpu-cargo.lock', cargo_lock)
    run('rust', [sys.executable, tree/'scripts/build-adreno-metal-bridge.py', '--target', 'ios'])
    if sha(cargo_lock) != sha(ROOT/'config/gpu-cargo.lock'):
        raise ValueError('Cargo dependency lock changed during build')
    run('turnip', [sys.executable, tree/'scripts/build-turnip-darwin.py', '--target', 'ios'])
    run('metal', tool('build_gpu_metal.py', '--tree', tree, '--turnip-tree', tree, '--output', out/'metal'))
    plan = {'candidate_sha256': sha(out/'linked/kernel.macho'),
            'original_executable_sha256': read(ROOT/'profiles/v91/image-build/plan.json')['prelink']['original_executable_sha256']}
    (out/'entry-plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    run('entry', tool('prepare_v91_entry.py', '--kernel', out/'linked/kernel.macho', '--plan', out/'entry-plan.json', '--clang', llvm/'clang', '--output', out/'entry'))
    files = [out/'entry/kernel.macho', out/'linked/native.macho', out/'linked/libEldenGpu.a',
             tree/'build/adreno-metal-bridge/ios/libadreno_metal_bridge.a',
             tree/'build/mesa-tu-ios/src/freedreno/vulkan/libvulkan_freedreno.dylib']
    files += [p for p in (out/'metal').rglob('*') if p.is_file() and (p.suffix in ('.plist', '.dylib') or p.name in ('NativeAdrenoMetal', 'adrenoprobed'))]
    result = {'complete_offline_build': True, 'baseline_revision': revision,
              'baseline_state_sha256': sha(base/'build-state.json'), 'prepared_sha256': sha(args.prepared/'prepared.json'),
              'source_revision': subprocess.check_output(['git', '-C', ROOT, 'rev-parse', 'HEAD'], text=True).strip(),
              'driver_script_sha256': sha(Path(__file__)), 'resumed_userspace': args.resume_userspace,
              'bison': {'path': str(bison), 'version': version},
              'gpu_specific_abi': read(out/'gpu-abi.json'), 'device_access': False,
              'hardware_qualified': False, 'boot_image_packaged': False,
              'artifacts': {p.relative_to(out).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in files}}
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS: complete offline chain and entry kernel; no device or rendering claim', flush=True)

if __name__ == '__main__':
    main()
